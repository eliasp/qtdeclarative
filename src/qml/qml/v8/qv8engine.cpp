/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtQml module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qv8engine_p.h"

#include "qv8contextwrapper_p.h"
#include "qv8valuetypewrapper_p.h"
#include "qv8sequencewrapper_p.h"
#include "qv8include_p.h"
#include "qjsengine_p.h"

#include <private/qqmlbuiltinfunctions_p.h>
#include <private/qqmllist_p.h>
#include <private/qqmlengine_p.h>
#include <private/qqmlxmlhttprequest_p.h>
#include <private/qqmllocale_p.h>
#include <private/qqmlglobal_p.h>
#include <private/qqmlmemoryprofiler_p.h>
#include <private/qqmlplatform_p.h>
#include <private/qjsconverter_p.h>

#include "qv4domerrors_p.h"
#include "qv4sqlerrors_p.h"

#include <QtCore/qjsonarray.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qjsonvalue.h>
#include <QtCore/qdatetime.h>

#include <private/qv4value_p.h>
#include <private/qv4dateobject_p.h>
#include <private/qv4objectiterator_p.h>
#include <private/qv4mm_p.h>
#include <private/qv4objectproto_p.h>
#include <private/qv4globalobject_p.h>

Q_DECLARE_METATYPE(QList<int>)


// XXX TODO: Need to check all the global functions will also work in a worker script where the
// QQmlEngine is not available
QT_BEGIN_NAMESPACE

static bool ObjectComparisonCallback(v8::Local<v8::Object> lhs, v8::Local<v8::Object> rhs)
{
    if (lhs == rhs)
        return true;

    if (lhs.IsEmpty() || rhs.IsEmpty())
        return false;

    QV8ObjectResource *lhsr = static_cast<QV8ObjectResource*>(lhs->GetExternalResource());
    QV8ObjectResource *rhsr = static_cast<QV8ObjectResource*>(rhs->GetExternalResource());

    if (lhsr && rhsr) {
        Q_ASSERT(lhsr->engine == rhsr->engine);
        QV8ObjectResource::ResourceType lhst = lhsr->resourceType();
        QV8ObjectResource::ResourceType rhst = rhsr->resourceType();

        switch (lhst) {
        case QV8ObjectResource::ValueTypeType:
            // a value type might be equal to a variant or another value type
            if (rhst == QV8ObjectResource::ValueTypeType) {
                return lhsr->engine->valueTypeWrapper()->isEqual(lhsr, lhsr->engine->valueTypeWrapper()->toVariant(rhsr));
            } else if (rhst == QV8ObjectResource::VariantType) {
                return lhsr->engine->valueTypeWrapper()->isEqual(lhsr, lhsr->engine->variantWrapper()->toVariant(rhsr));
            }
            break;
        case QV8ObjectResource::VariantType:
            // a variant might be equal to a value type or other variant.
            if (rhst == QV8ObjectResource::VariantType) {
                return lhsr->engine->variantWrapper()->toVariant(lhsr) ==
                       lhsr->engine->variantWrapper()->toVariant(rhsr);
            } else if (rhst == QV8ObjectResource::ValueTypeType) {
                return rhsr->engine->valueTypeWrapper()->isEqual(rhsr, rhsr->engine->variantWrapper()->toVariant(lhsr));
            }
            break;
        case QV8ObjectResource::SequenceType:
            // a sequence might be equal to itself.
            if (rhst == QV8ObjectResource::SequenceType) {
                return lhsr->engine->sequenceWrapper()->isEqual(lhsr, rhsr);
            }
            break;
        default:
            break;
        }
    }

    return false;
}


QV8Engine::QV8Engine(QJSEngine* qq, ContextOwnership ownership)
    : q(qq)
    , m_engine(0)
    , m_ownsV8Context(ownership == CreateNewContext)
    , m_xmlHttpRequestData(0)
    , m_listModelData(0)
    , m_platform(0)
    , m_application(0)
{
    QML_MEMORY_SCOPE_STRING("QV8Engine::QV8Engine");
    qMetaTypeId<QJSValue>();
    qMetaTypeId<QList<int> >();

    QByteArray v8args = qgetenv("V8ARGS");
    // change default v8 behaviour to not relocate breakpoints across lines
    if (!v8args.contains("breakpoint_relocation"))
        v8args.append(" --nobreakpoint_relocation");
    v8::V8::SetFlagsFromString(v8args.constData(), v8args.length());

    ensurePerThreadIsolate();

    v8::HandleScope handle_scope;
    m_context = (ownership == CreateNewContext) ? v8::Context::New() : v8::Persistent<v8::Context>::New(v8::Context::GetCurrent());
    qPersistentRegister(m_context);
    v8::Context::Scope context_scope(m_context);

    m_v4Engine = v8::Isolate::GetEngine();
    m_v4Engine->publicEngine = q;

    v8::V8::SetUserObjectComparisonCallbackFunction(ObjectComparisonCallback);
    QV8GCCallback::registerGcPrologueCallback();
    m_strongReferencer = qPersistentNew(v8::Object::New());

    m_bindingFlagKey = qPersistentNew(v8::String::New("qml::binding"));

    m_contextWrapper.init(this);
    m_qobjectWrapper.init(this);
    m_typeWrapper.init(this);
    m_listWrapper.init(this);
    m_variantWrapper.init(this);
    m_valueTypeWrapper.init(this);
    m_sequenceWrapper.init(this);
    m_jsonWrapper.init(m_v4Engine);

}

QV8Engine::~QV8Engine()
{
    Q_ASSERT_X(v8::Isolate::GetCurrent(), "QV8Engine::~QV8Engine()", "called after v8::Isolate has exited");
    for (int ii = 0; ii < m_extensionData.count(); ++ii)
        delete m_extensionData[ii];
    m_extensionData.clear();

    qt_rem_qmlxmlhttprequest(this, m_xmlHttpRequestData);
    m_xmlHttpRequestData = 0;
    delete m_listModelData;
    m_listModelData = 0;

    qPersistentDispose(m_strongReferencer);

    m_jsonWrapper.destroy();
    m_sequenceWrapper.destroy();
    m_valueTypeWrapper.destroy();
    m_variantWrapper.destroy();
    m_listWrapper.destroy();
    m_typeWrapper.destroy();
    m_qobjectWrapper.destroy();
    m_contextWrapper.destroy();

    qPersistentDispose(m_bindingFlagKey);

    if (m_ownsV8Context)
        qPersistentDispose(m_context);
}

QVariant QV8Engine::toVariant(v8::Handle<v8::Value> value, int typeHint)
{
    if (value.IsEmpty())
        return QVariant();

    if (typeHint == QVariant::Bool)
        return QVariant(value->BooleanValue());

    if (typeHint == QMetaType::QJsonValue)
        return QVariant::fromValue(jsonValueFromJS(value));

    if (typeHint == qMetaTypeId<QJSValue>())
        return QVariant::fromValue(scriptValueFromInternal(value));

    if (value->IsObject()) {
        QV8ObjectResource *r = (QV8ObjectResource *)value->ToObject()->GetExternalResource();
        if (r) {
            switch (r->resourceType()) {
            case QV8ObjectResource::Context2DStyleType:
            case QV8ObjectResource::Context2DPixelArrayType:
            case QV8ObjectResource::SignalHandlerType:
            case QV8ObjectResource::IncubatorType:
            case QV8ObjectResource::VisualDataItemType:
            case QV8ObjectResource::ContextType:
            case QV8ObjectResource::XMLHttpRequestType:
            case QV8ObjectResource::DOMNodeType:
            case QV8ObjectResource::SQLDatabaseType:
            case QV8ObjectResource::ListModelType:
            case QV8ObjectResource::Context2DType:
            case QV8ObjectResource::ParticleDataType:
            case QV8ObjectResource::LocaleDataType:
            case QV8ObjectResource::ChangeSetArrayType:
                return QVariant();
            case QV8ObjectResource::TypeType:
                return m_typeWrapper.toVariant(r);
            case QV8ObjectResource::QObjectType:
                return qVariantFromValue<QObject *>(m_qobjectWrapper.toQObject(r));
            case QV8ObjectResource::ListType:
                return m_listWrapper.toVariant(r);
            case QV8ObjectResource::VariantType:
                return m_variantWrapper.toVariant(r);
            case QV8ObjectResource::ValueTypeType:
                return m_valueTypeWrapper.toVariant(r);
            case QV8ObjectResource::SequenceType:
                return m_sequenceWrapper.toVariant(r);
            }
        } else if (typeHint == QMetaType::QJsonObject
                   && !value->IsArray() && !value->IsFunction()) {
            return QVariant::fromValue(jsonObjectFromJS(value));
        }
    }

    if (value->IsArray()) {
        v8::Handle<v8::Array> array = v8::Handle<v8::Array>::Cast(value);
        if (typeHint == qMetaTypeId<QList<QObject *> >()) {
            QList<QObject *> list;
            uint32_t length = array->Length();
            for (uint32_t ii = 0; ii < length; ++ii) {
                v8::Local<v8::Value> arrayItem = array->Get(ii);
                if (arrayItem->IsObject()) {
                    list << toQObject(arrayItem->ToObject());
                } else {
                    list << 0;
                }
            }

            return qVariantFromValue<QList<QObject*> >(list);
        } else if (typeHint == QMetaType::QJsonArray) {
            return QVariant::fromValue(jsonArrayFromJS(value));
        }

        bool succeeded = false;
        QVariant retn = m_sequenceWrapper.toVariant(array, typeHint, &succeeded);
        if (succeeded)
            return retn;
    }

    return toBasicVariant(value);
}

static v8::Handle<v8::Array> arrayFromStringList(QV8Engine *engine, const QStringList &list)
{
    v8::Context::Scope scope(engine->context());
    v8::Local<v8::Array> result = v8::Array::New(list.count());
    for (int ii = 0; ii < list.count(); ++ii)
        result->Set(ii, engine->toString(list.at(ii)));
    return result;
}

static v8::Handle<v8::Array> arrayFromVariantList(QV8Engine *engine, const QVariantList &list)
{
    v8::Context::Scope scope(engine->context());
    v8::Local<v8::Array> result = v8::Array::New(list.count());
    for (int ii = 0; ii < list.count(); ++ii)
        result->Set(ii, engine->fromVariant(list.at(ii)));
    return result;
}

static v8::Handle<v8::Object> objectFromVariantMap(QV8Engine *engine, const QVariantMap &map)
{
    v8::Context::Scope scope(engine->context());
    v8::Local<v8::Object> object = v8::Object::New();
    for (QVariantMap::ConstIterator iter = map.begin(); iter != map.end(); ++iter)
        object->Set(engine->toString(iter.key()), engine->fromVariant(iter.value()));
    return object;
}

Q_CORE_EXPORT QString qt_regexp_toCanonical(const QString &, QRegExp::PatternSyntax);

v8::Handle<v8::Value> QV8Engine::fromVariant(const QVariant &variant)
{
    int type = variant.userType();
    const void *ptr = variant.constData();

    if (type < QMetaType::User) {
        switch (QMetaType::Type(type)) {
            case QMetaType::UnknownType:
            case QMetaType::Void:
                return v8::Undefined();
            case QMetaType::Bool:
                return v8::Boolean::New(*reinterpret_cast<const bool*>(ptr));
            case QMetaType::Int:
                return v8::Integer::New(*reinterpret_cast<const int*>(ptr));
            case QMetaType::UInt:
                return v8::Integer::NewFromUnsigned(*reinterpret_cast<const uint*>(ptr));
            case QMetaType::LongLong:
                return v8::Number::New(*reinterpret_cast<const qlonglong*>(ptr));
            case QMetaType::ULongLong:
                return v8::Number::New(*reinterpret_cast<const qulonglong*>(ptr));
            case QMetaType::Double:
                return v8::Number::New(*reinterpret_cast<const double*>(ptr));
            case QMetaType::QString:
                return v8::Value::fromV4Value(QV4::Value::fromString(m_v4Engine->current, *reinterpret_cast<const QString*>(ptr)));
            case QMetaType::Float:
                return v8::Number::New(*reinterpret_cast<const float*>(ptr));
            case QMetaType::Short:
                return v8::Integer::New(*reinterpret_cast<const short*>(ptr));
            case QMetaType::UShort:
                return v8::Integer::NewFromUnsigned(*reinterpret_cast<const unsigned short*>(ptr));
            case QMetaType::Char:
                return v8::Integer::New(*reinterpret_cast<const char*>(ptr));
            case QMetaType::UChar:
                return v8::Integer::NewFromUnsigned(*reinterpret_cast<const unsigned char*>(ptr));
            case QMetaType::QChar:
                return v8::Integer::New((*reinterpret_cast<const QChar*>(ptr)).unicode());
            case QMetaType::QDateTime:
                return v8::Date::New(qtDateTimeToJsDate(*reinterpret_cast<const QDateTime *>(ptr)));
            case QMetaType::QDate:
                return v8::Date::New(qtDateTimeToJsDate(QDateTime(*reinterpret_cast<const QDate *>(ptr))));
            case QMetaType::QTime:
                return v8::Date::New(qtDateTimeToJsDate(QDateTime(QDate(1970,1,1), *reinterpret_cast<const QTime *>(ptr))));
            case QMetaType::QRegExp:
                return QJSConverter::toRegExp(*reinterpret_cast<const QRegExp *>(ptr));
            case QMetaType::QObjectStar:
                return newQObject(*reinterpret_cast<QObject* const *>(ptr));
            case QMetaType::QStringList:
                {
                bool succeeded = false;
                v8::Handle<v8::Value> retn = m_sequenceWrapper.fromVariant(variant, &succeeded);
                if (succeeded)
                    return retn;
                return arrayFromStringList(this, *reinterpret_cast<const QStringList *>(ptr));
                }
            case QMetaType::QVariantList:
                return arrayFromVariantList(this, *reinterpret_cast<const QVariantList *>(ptr));
            case QMetaType::QVariantMap:
                return objectFromVariantMap(this, *reinterpret_cast<const QVariantMap *>(ptr));
            case QMetaType::QJsonValue:
                return jsonValueToJS(*reinterpret_cast<const QJsonValue *>(ptr));
            case QMetaType::QJsonObject:
                return jsonObjectToJS(*reinterpret_cast<const QJsonObject *>(ptr));
            case QMetaType::QJsonArray:
                return jsonArrayToJS(*reinterpret_cast<const QJsonArray *>(ptr));

            default:
                break;
        }

        if (QQmlValueType *vt = QQmlValueTypeFactory::valueType(type))
            return m_valueTypeWrapper.newValueType(variant, vt);
    } else {
        if (type == qMetaTypeId<QQmlListReference>()) {
            typedef QQmlListReferencePrivate QDLRP;
            QDLRP *p = QDLRP::get((QQmlListReference*)ptr);
            if (p->object) {
                return m_listWrapper.newList(p->property, p->propertyType);
            } else {
                return v8::Null();
            }
        } else if (type == qMetaTypeId<QJSValue>()) {
            const QJSValue *value = reinterpret_cast<const QJSValue *>(ptr);
            QJSValuePrivate *valuep = QJSValuePrivate::get(*value);
            valuep->engine = m_v4Engine;
                return v8::Local<v8::Value>::New(v8::Value::fromV4Value(valuep->getValue(valuep->engine)));
//            if (valuep->assignEngine(this))
//                return v8::Local<v8::Value>::New(*valuep);
        } else if (type == qMetaTypeId<QList<QObject *> >()) {
            // XXX Can this be made more by using Array as a prototype and implementing
            // directly against QList<QObject*>?
            const QList<QObject *> &list = *(QList<QObject *>*)ptr;
            v8::Local<v8::Array> array = v8::Array::New(list.count());
            for (int ii = 0; ii < list.count(); ++ii)
                array->Set(ii, newQObject(list.at(ii)));
            return array;
        }

        bool objOk;
        QObject *obj = QQmlMetaType::toQObject(variant, &objOk);
        if (objOk)
            return newQObject(obj);

        bool succeeded = false;
        v8::Handle<v8::Value> retn = m_sequenceWrapper.fromVariant(variant, &succeeded);
        if (succeeded)
            return retn;

        if (QQmlValueType *vt = QQmlValueTypeFactory::valueType(type))
            return m_valueTypeWrapper.newValueType(variant, vt);
    }

    // XXX TODO: To be compatible, we still need to handle:
    //    + QObjectList
    //    + QList<int>

    return m_variantWrapper.newVariant(variant);
}

// A handle scope and context must be entered
v8::Local<v8::Script> QV8Engine::qmlModeCompile(const QString &source,
                                                const QString &fileName,
                                                quint16 lineNumber)
{
    v8::Local<v8::String> v8source = QJSConverter::toString(source);
    v8::Local<v8::String> v8fileName = QJSConverter::toString(fileName);

    v8::ScriptOrigin origin(v8fileName, v8::Integer::New(lineNumber - 1));

    v8::Local<v8::Script> script = v8::Script::Compile(v8source, &origin, 0, v8::Handle<v8::String>(),
                                                       v8::Script::QmlMode);

    return script;
}

// A handle scope and context must be entered.
// source can be either ascii or utf8.
v8::Local<v8::Script> QV8Engine::qmlModeCompile(const char *source, int sourceLength,
                                                const QString &fileName,
                                                quint16 lineNumber)
{
    if (sourceLength == -1)
        sourceLength = int(strlen(source));

    v8::Local<v8::String> v8source = v8::String::New(source, sourceLength);
    v8::Local<v8::String> v8fileName = QJSConverter::toString(fileName);

    v8::ScriptOrigin origin(v8fileName, v8::Integer::New(lineNumber - 1));

    v8::Local<v8::Script> script = v8::Script::Compile(v8source, &origin, 0, v8::Handle<v8::String>(),
                                                       v8::Script::QmlMode);

    return script;
}

QNetworkAccessManager *QV8Engine::networkAccessManager()
{
    return QQmlEnginePrivate::get(m_engine)->getNetworkAccessManager();
}

const QStringHash<bool> &QV8Engine::illegalNames() const
{
    return m_illegalNames;
}

// Requires a handle scope
QV4::Value QV8Engine::getOwnPropertyNames(const QV4::Value &o)
{
    if (!o.asObject())
        return QV4::Value::fromObject(m_v4Engine->newArrayObject(m_v4Engine->current));
    QV4::SimpleCallContext ctx;
    QV4::Value args = o;
    ctx.arguments = &args;
    ctx.argumentCount = 1;
    ctx.engine = m_v4Engine;
    ctx.parent = m_v4Engine->current;
    QV4::Value result = QV4::ObjectPrototype::method_getOwnPropertyNames(&ctx);
    return result;
}

QQmlContextData *QV8Engine::callingContext()
{
    return m_contextWrapper.callingContext();
}

// Converts a JS value to a QVariant.
// Null, Undefined -> QVariant() (invalid)
// Boolean -> QVariant(bool)
// Number -> QVariant(double)
// String -> QVariant(QString)
// Array -> QVariantList(...)
// Date -> QVariant(QDateTime)
// RegExp -> QVariant(QRegExp)
// [Any other object] -> QVariantMap(...)
QVariant QV8Engine::toBasicVariant(v8::Handle<v8::Value> value)
{
    if (value->IsNull() || value->IsUndefined())
        return QVariant();
    if (value->IsBoolean())
        return value->ToBoolean()->Value();
    if (value->IsInt32())
        return value->ToInt32()->Value();
    if (value->IsNumber())
        return value->ToNumber()->Value();
    if (value->IsString())
        return value.get()->v4Value().toString(m_v4Engine->current)->toQString();
    if (value->IsDate())
        return qtDateTimeFromJsDate(v8::Handle<v8::Date>::Cast(value)->NumberValue());
    // NOTE: since we convert QTime to JS Date, round trip will change the variant type (to QDateTime)!

    Q_ASSERT(value->IsObject());

    if (value->IsRegExp()) {
        v8::Context::Scope scope(context());
        return QJSConverter::toRegExp(v8::Handle<v8::RegExp>::Cast(value));
    }
    if (value->IsArray()) {
        v8::Context::Scope scope(context());
        QVariantList rv;

        v8::Handle<v8::Array> array = v8::Handle<v8::Array>::Cast(value);
        int length = array->Length();
        for (int ii = 0; ii < length; ++ii)
            rv << toVariant(array->Get(ii), -1);
        return rv;
    }
    if (!value->IsFunction()) {
        v8::Context::Scope scope(context());
        v8::Handle<v8::Object> object = value->ToObject();
        return variantMapFromJS(object->v4Value().asObject());
    }

    return QVariant();
}



struct StaticQtMetaObject : public QObject
{
    static const QMetaObject *get()
        { return &staticQtMetaObject; }
};

void QV8Engine::initializeGlobal(v8::Handle<v8::Object> global)
{
    using namespace QQmlBuiltinFunctions;

    v8::Local<v8::Object> console = v8::Object::New();
    v8::Local<v8::Function> consoleLogFn = V8FUNCTION(consoleLog, this);

    console->Set(v8::String::New("debug"), consoleLogFn);
    console->Set(v8::String::New("log"), consoleLogFn);
    console->Set(v8::String::New("info"), consoleLogFn);
    console->Set(v8::String::New("warn"), V8FUNCTION(consoleWarn, this));
    console->Set(v8::String::New("error"), V8FUNCTION(consoleError, this));
    console->Set(v8::String::New("assert"), V8FUNCTION(consoleAssert, this));

    console->Set(v8::String::New("count"), V8FUNCTION(consoleCount, this));
    console->Set(v8::String::New("profile"), V8FUNCTION(consoleProfile, this));
    console->Set(v8::String::New("profileEnd"), V8FUNCTION(consoleProfileEnd, this));
    console->Set(v8::String::New("time"), V8FUNCTION(consoleTime, this));
    console->Set(v8::String::New("timeEnd"), V8FUNCTION(consoleTimeEnd, this));
    console->Set(v8::String::New("trace"), V8FUNCTION(consoleTrace, this));
    console->Set(v8::String::New("exception"), V8FUNCTION(consoleException, this));

    v8::Local<v8::Object> qt = v8::Object::New();

    // Set all the enums from the "Qt" namespace
    const QMetaObject *qtMetaObject = StaticQtMetaObject::get();
    for (int ii = 0; ii < qtMetaObject->enumeratorCount(); ++ii) {
        QMetaEnum enumerator = qtMetaObject->enumerator(ii);
        for (int jj = 0; jj < enumerator.keyCount(); ++jj) {
            qt->Set(v8::String::New(enumerator.key(jj)), v8::Integer::New(enumerator.value(jj)));
        }
    }
    qt->Set(v8::String::New("Asynchronous"), v8::Integer::New(0));
    qt->Set(v8::String::New("Synchronous"), v8::Integer::New(1));

    qt->Set(v8::String::New("include"), V8FUNCTION(QV8Include::include, this));
    qt->Set(v8::String::New("isQtObject"), V8FUNCTION(isQtObject, this));
    qt->Set(v8::String::New("rgba"), V8FUNCTION(rgba, this));
    qt->Set(v8::String::New("hsla"), V8FUNCTION(hsla, this));
    qt->Set(v8::String::New("colorEqual"), V8FUNCTION(colorEqual, this));
    qt->Set(v8::String::New("font"), V8FUNCTION(font, this));
    qt->Set(v8::String::New("rect"), V8FUNCTION(rect, this));
    qt->Set(v8::String::New("point"), V8FUNCTION(point, this));
    qt->Set(v8::String::New("size"), V8FUNCTION(size, this));

    qt->Set(v8::String::New("vector2d"), V8FUNCTION(vector2d, this));
    qt->Set(v8::String::New("vector3d"), V8FUNCTION(vector3d, this));
    qt->Set(v8::String::New("vector4d"), V8FUNCTION(vector4d, this));
    qt->Set(v8::String::New("quaternion"), V8FUNCTION(quaternion, this));
    qt->Set(v8::String::New("matrix4x4"), V8FUNCTION(matrix4x4, this));

    qt->Set(v8::String::New("formatDate"), V8FUNCTION(formatDate, this));
    qt->Set(v8::String::New("formatTime"), V8FUNCTION(formatTime, this));
    qt->Set(v8::String::New("formatDateTime"), V8FUNCTION(formatDateTime, this));

    qt->Set(v8::String::New("openUrlExternally"), V8FUNCTION(openUrlExternally, this));
    qt->Set(v8::String::New("fontFamilies"), V8FUNCTION(fontFamilies, this));
    qt->Set(v8::String::New("md5"), V8FUNCTION(md5, this));
    qt->Set(v8::String::New("btoa"), V8FUNCTION(btoa, this));
    qt->Set(v8::String::New("atob"), V8FUNCTION(atob, this));
    qt->Set(v8::String::New("resolvedUrl"), V8FUNCTION(resolvedUrl, this));
    qt->Set(v8::String::New("locale"), V8FUNCTION(locale, this));
    qt->Set(v8::String::New("binding"), V8FUNCTION(binding, this));

    if (m_engine) {
        qt->SetAccessor(v8::String::New("platform"), getPlatform, 0, v8::External::New(this));
        qt->SetAccessor(v8::String::New("application"), getApplication, 0, v8::External::New(this));
#ifndef QT_NO_IM
        qt->SetAccessor(v8::String::New("inputMethod"), getInputMethod, 0, v8::External::New(this));
#endif
        qt->Set(v8::String::New("lighter"), V8FUNCTION(lighter, this));
        qt->Set(v8::String::New("darker"), V8FUNCTION(darker, this));
        qt->Set(v8::String::New("tint"), V8FUNCTION(tint, this));
        qt->Set(v8::String::New("quit"), V8FUNCTION(quit, this));
        qt->Set(v8::String::New("createQmlObject"), V8FUNCTION(createQmlObject, this));
        qt->Set(v8::String::New("createComponent"), V8FUNCTION(createComponent, this));
    }

#ifndef QT_NO_TRANSLATION
    global->Set(v8::String::New("qsTranslate"), V8FUNCTION(qsTranslate, this));
    global->Set(v8::String::New("QT_TRANSLATE_NOOP"), V8FUNCTION(qsTranslateNoOp, this));
    global->Set(v8::String::New("qsTr"), V8FUNCTION(qsTr, this));
    global->Set(v8::String::New("QT_TR_NOOP"), V8FUNCTION(qsTrNoOp, this));
    global->Set(v8::String::New("qsTrId"), V8FUNCTION(qsTrId, this));
    global->Set(v8::String::New("QT_TRID_NOOP"), V8FUNCTION(qsTrIdNoOp, this));
#endif

    global->Set(v8::String::New("print"), consoleLogFn);
    global->Set(v8::String::New("console"), console);
    global->Set(v8::String::New("Qt"), qt);
    global->Set(v8::String::New("gc"), V8FUNCTION(QQmlBuiltinFunctions::gc, this));

    {
#define STRING_ARG "(function(stringArg) { "\
                   "    String.prototype.arg = (function() {"\
                   "        return stringArg.apply(this, arguments);"\
                   "    })"\
                   "})"

        v8::Local<v8::Script> registerArg = v8::Script::New(v8::String::New(STRING_ARG), 0, 0, v8::Handle<v8::String>(), v8::Script::NativeMode);
        v8::Local<v8::Value> result = registerArg->Run();
        Q_ASSERT(result->IsFunction());
        v8::Local<v8::Function> registerArgFunc = v8::Local<v8::Function>::Cast(result);
        v8::Handle<v8::Value> args = V8FUNCTION(stringArg, this);
        registerArgFunc->Call(v8::Local<v8::Object>::Cast(registerArgFunc), 1, &args);
#undef STRING_ARG
    }

    QQmlLocale::registerStringLocaleCompare(this);
    QQmlDateExtension::registerExtension(this);
    QQmlNumberExtension::registerExtension(this);

    qt_add_domexceptions(m_v4Engine);
    m_xmlHttpRequestData = qt_add_qmlxmlhttprequest(this);

    qt_add_sqlexceptions(m_v4Engine);

    {
        for (uint i = 0; i < m_v4Engine->globalObject->internalClass->size; ++i)
            m_illegalNames.insert(m_v4Engine->globalObject->internalClass->nameMap.at(i)->toQString(), true);
    }

    {
#define FREEZE_SOURCE "(function freeze_recur(obj) { "\
                      "    if (Qt.isQtObject(obj)) return;"\
                      "    if (obj != Function.connect && obj != Function.disconnect && "\
                      "        obj instanceof Object) {"\
                      "        var properties = Object.getOwnPropertyNames(obj);"\
                      "        for (var prop in properties) { "\
                      "            if (prop == \"connect\" || prop == \"disconnect\") {"\
                      "                Object.freeze(obj[prop]); "\
                      "                continue;"\
                      "            }"\
                      "            freeze_recur(obj[prop]);"\
                      "        }"\
                      "    }"\
                      "    if (obj instanceof Object) {"\
                      "        Object.freeze(obj);"\
                      "    }"\
                      "})"

        QV4::Value result = evaluateScript(QStringLiteral(FREEZE_SOURCE), 0);
        Q_ASSERT(result.asFunctionObject());
        m_freezeObject = QV4::PersistentValue(m_v4Engine, result);
#undef FREEZE_SOURCE
    }
}

void QV8Engine::freezeObject(const QV4::Value &value)
{
    QV4::Value args = value;
    m_freezeObject->asFunctionObject()->call(m_v4Engine->rootContext, QV4::Value::fromObject(m_v4Engine->globalObject), &args, 1);
}

void QV8Engine::gc()
{
    m_v4Engine->memoryManager->runGC();
}

#ifdef QML_GLOBAL_HANDLE_DEBUGGING
#include <QtCore/qthreadstorage.h>
static QThreadStorage<QSet<void *> *> QV8Engine_activeHandles;

void QV8Engine::registerHandle(void *handle)
{
    if (!handle) {
        qWarning("Attempting to register a null handle");
        return;
    }

    if (!QV8Engine_activeHandles.hasLocalData())
        QV8Engine_activeHandles.setLocalData(new QSet<void *>);

    if (QV8Engine_activeHandles.localData()->contains(handle)) {
        qFatal("Handle %p already alive", handle);
    } else {
        QV8Engine_activeHandles.localData()->insert(handle);
    }
}

void QV8Engine::releaseHandle(void *handle)
{
    if (!handle)
        return;

    if (!QV8Engine_activeHandles.hasLocalData())
        QV8Engine_activeHandles.setLocalData(new QSet<void *>);

    if (QV8Engine_activeHandles.localData()->contains(handle)) {
        QV8Engine_activeHandles.localData()->remove(handle);
    } else {
        qFatal("Handle %p already dead", handle);
    }
}
#endif

struct QV8EngineRegistrationData
{
    QV8EngineRegistrationData() : extensionCount(0) {}

    QMutex mutex;
    int extensionCount;
};
Q_GLOBAL_STATIC(QV8EngineRegistrationData, registrationData);

QMutex *QV8Engine::registrationMutex()
{
    return &registrationData()->mutex;
}

int QV8Engine::registerExtension()
{
    return registrationData()->extensionCount++;
}

void QV8Engine::setExtensionData(int index, Deletable *data)
{
    if (m_extensionData.count() <= index)
        m_extensionData.resize(index + 1);

    if (m_extensionData.at(index))
        delete m_extensionData.at(index);

    m_extensionData[index] = data;
}

double QV8Engine::qtDateTimeToJsDate(const QDateTime &dt)
{
    if (!dt.isValid()) {
        return qSNaN();
    }

    return dt.toMSecsSinceEpoch();
}

QDateTime QV8Engine::qtDateTimeFromJsDate(double jsDate)
{
    if (qIsNaN(jsDate))
        return QDateTime();

    return QDateTime::fromMSecsSinceEpoch(jsDate);
}

v8::Persistent<v8::Object> *QV8Engine::findOwnerAndStrength(QObject *object, bool *shouldBeStrong)
{
    QQmlData *data = QQmlData::get(object);
    if (data && data->rootObjectInCreation) { // When the object is still being created it may not show up to the GC.
        *shouldBeStrong = true;
        return 0;
    }

    QObject *parent = object->parent();
    if (!parent) {
        // if the object has JS ownership, the object's v8object owns the lifetime of the persistent value.
        if (QQmlEngine::objectOwnership(object) == QQmlEngine::JavaScriptOwnership) {
            *shouldBeStrong = false;
            return &(QQmlData::get(object)->v8object);
        }

        // no parent, and has CPP ownership - doesn't have an implicit parent.
        *shouldBeStrong = true;
        return 0;
    }

    // if it is owned by CPP, it's root parent may still be owned by JS.
    // in that case, the owner of the persistent handle is the root parent's v8object.
    while (parent->parent())
        parent = parent->parent();

    if (QQmlEngine::objectOwnership(parent) == QQmlEngine::JavaScriptOwnership) {
        // root parent is owned by JS.  It's v8object owns the persistent value in question.
        *shouldBeStrong = false;
        return &(QQmlData::get(parent)->v8object);
    } else {
        // root parent has CPP ownership.  The persistent value should not be made weak.
        *shouldBeStrong = true;
        return 0;
    }
}

void QV8Engine::addRelationshipForGC(QObject *object, v8::Persistent<v8::Value> handle)
{
    if (!object || handle.IsEmpty())
        return;

    bool handleShouldBeStrong = false;
    v8::Persistent<v8::Object> *implicitOwner = findOwnerAndStrength(object, &handleShouldBeStrong);
    if (handleShouldBeStrong) {
        v8::V8::AddImplicitReferences(m_strongReferencer, &handle, 1);
    } else if (!implicitOwner->IsEmpty()) {
        v8::V8::AddImplicitReferences(*implicitOwner, &handle, 1);
    }
}

void QV8Engine::addRelationshipForGC(QObject *object, QObject *other)
{
    if (!object || !other)
        return;

    bool handleShouldBeStrong = false;
    v8::Persistent<v8::Object> *implicitOwner = findOwnerAndStrength(object, &handleShouldBeStrong);
    v8::Persistent<v8::Value> handle = QQmlData::get(other, true)->v8object;
    if (handle.IsEmpty()) // no JS data to keep alive.
        return;
    else if (handleShouldBeStrong)
        v8::V8::AddImplicitReferences(m_strongReferencer, &handle, 1);
    else if (!implicitOwner->IsEmpty())
        v8::V8::AddImplicitReferences(*implicitOwner, &handle, 1);
}

static QThreadStorage<QV8Engine::ThreadData*> perThreadEngineData;

bool QV8Engine::hasThreadData()
{
    return perThreadEngineData.hasLocalData();
}

QV8Engine::ThreadData *QV8Engine::threadData()
{
    Q_ASSERT(perThreadEngineData.hasLocalData());
    return perThreadEngineData.localData();
}

void QV8Engine::ensurePerThreadIsolate()
{
    if (!perThreadEngineData.hasLocalData())
        perThreadEngineData.setLocalData(new ThreadData);
}

void QV8Engine::initQmlGlobalObject()
{
    v8::HandleScope handels;
    v8::Context::Scope contextScope(m_context);
    initializeGlobal(m_context->Global());
    freezeObject(QV4::Value::fromObject(m_v4Engine->globalObject));
}

void QV8Engine::setEngine(QQmlEngine *engine)
{
    m_engine = engine;
    initQmlGlobalObject();
}

v8::Handle<v8::Value> QV8Engine::throwException(v8::Handle<v8::Value> value)
{
    __qmljs_throw(m_v4Engine->current, value->v4Value());
    return value;
}

// Converts a QVariantList to JS.
// The result is a new Array object with length equal to the length
// of the QVariantList, and the elements being the QVariantList's
// elements converted to JS, recursively.
QV4::Value QV8Engine::variantListToJS(const QVariantList &lst)
{
    QV4::ArrayObject *a = m_v4Engine->newArrayObject(m_v4Engine->current);
    a->arrayReserve(lst.size());
    for (int i = 0; i < lst.size(); i++)
        a->arrayData[i].value = variantToJS(lst.at(i));
    a->setArrayLengthUnchecked(lst.size());
    return QV4::Value::fromObject(a);
}

// Converts a JS Array object to a QVariantList.
// The result is a QVariantList with length equal to the length
// of the JS Array, and elements being the JS Array's elements
// converted to QVariants, recursively.
QVariantList QV8Engine::variantListFromJS(QV4::ArrayObject *a,
                                          V8ObjectSet &visitedObjects)
{
    QVariantList result;
    if (!a)
        return result;

    if (visitedObjects.contains(a))
        // Avoid recursion.
        return result;

    visitedObjects.insert(a);

    quint32 length = a->arrayLength();
    for (quint32 i = 0; i < length; ++i) {
        QV4::Value v = a->getIndexed(m_v4Engine->current, i);
        result.append(variantFromJS(v, visitedObjects));
    }

    visitedObjects.remove(a);

    return result;
}

// Converts a QVariantMap to JS.
// The result is a new Object object with property names being
// the keys of the QVariantMap, and values being the values of
// the QVariantMap converted to JS, recursively.
QV4::Value QV8Engine::variantMapToJS(const QVariantMap &vmap)
{
    QV4::Object *o = m_v4Engine->newObject();
    QVariantMap::const_iterator it;
    for (it = vmap.constBegin(); it != vmap.constEnd(); ++it) {
        QV4::Property *p = o->insertMember(m_v4Engine->newIdentifier(it.key()), QV4::Attr_Data);
        p->value = variantToJS(it.value());
    }
    return QV4::Value::fromObject(o);
}

// Converts a JS Object to a QVariantMap.
// The result is a QVariantMap with keys being the property names
// of the object, and values being the values of the JS object's
// properties converted to QVariants, recursively.
QVariantMap QV8Engine::variantMapFromJS(QV4::Object *o,
                                        V8ObjectSet &visitedObjects)
{
    QVariantMap result;

    if (!o || o->asFunctionObject())
        return result;

    if (visitedObjects.contains(o)) {
        // Avoid recursion.
        // For compatibility with QVariant{List,Map} conversion, we return an
        // empty object (and no error is thrown).
        return result;
    }

    visitedObjects.insert(o);

    QV4::ObjectIterator it(o, QV4::ObjectIterator::EnumberableOnly);
    while (1) {
        QV4::PropertyAttributes attributes;
        QV4::String *name;
        uint idx;
        QV4::Property *p = it.next(&name, &idx, &attributes);
        if (!p)
            break;

        QV4::Value v = o->getValue(m_v4Engine->current, p, attributes);
        QString key = name ? name->toQString() : QString::number(idx);
        result.insert(key, variantFromJS(v, visitedObjects));
    }

    visitedObjects.remove(o);
    return result;
}

// Converts the meta-type defined by the given type and data to JS.
// Returns the value if conversion succeeded, an empty handle otherwise.
QV4::Value QV8Engine::metaTypeToJS(int type, const void *data)
{
    Q_ASSERT(data != 0);
    QV4::Value result;

    // check if it's one of the types we know
    switch (QMetaType::Type(type)) {
    case QMetaType::UnknownType:
    case QMetaType::Void:
        return QV4::Value::undefinedValue();
    case QMetaType::Bool:
        return QV4::Value::fromBoolean(*reinterpret_cast<const bool*>(data));
    case QMetaType::Int:
        return QV4::Value::fromInt32(*reinterpret_cast<const int*>(data));
    case QMetaType::UInt:
        return QV4::Value::fromUInt32(*reinterpret_cast<const uint*>(data));
    case QMetaType::LongLong:
        return QV4::Value::fromDouble(double(*reinterpret_cast<const qlonglong*>(data)));
    case QMetaType::ULongLong:
#if defined(Q_OS_WIN) && defined(_MSC_FULL_VER) && _MSC_FULL_VER <= 12008804
#pragma message("** NOTE: You need the Visual Studio Processor Pack to compile support for 64bit unsigned integers.")
        return QV4::Value::fromDouble(double((qlonglong)*reinterpret_cast<const qulonglong*>(data)));
#elif defined(Q_CC_MSVC) && !defined(Q_CC_MSVC_NET)
        return QV4::Value::fromDouble(double((qlonglong)*reinterpret_cast<const qulonglong*>(data)));
#else
        return QV4::Value::fromDouble(double(*reinterpret_cast<const qulonglong*>(data)));
#endif
    case QMetaType::Double:
        return QV4::Value::fromDouble(*reinterpret_cast<const double*>(data));
    case QMetaType::QString:
        return QV4::Value::fromString(m_v4Engine->current, *reinterpret_cast<const QString*>(data));
    case QMetaType::Float:
        return QV4::Value::fromDouble(*reinterpret_cast<const float*>(data));
    case QMetaType::Short:
        return QV4::Value::fromInt32(*reinterpret_cast<const short*>(data));
    case QMetaType::UShort:
        return QV4::Value::fromUInt32(*reinterpret_cast<const unsigned short*>(data));
    case QMetaType::Char:
        return QV4::Value::fromInt32(*reinterpret_cast<const char*>(data));
    case QMetaType::UChar:
        return QV4::Value::fromUInt32(*reinterpret_cast<const unsigned char*>(data));
    case QMetaType::QChar:
        return QV4::Value::fromUInt32((*reinterpret_cast<const QChar*>(data)).unicode());
    case QMetaType::QStringList:
        result = QJSConverter::toStringList(*reinterpret_cast<const QStringList *>(data))->v4Value();
        break;
    case QMetaType::QVariantList:
        result = variantListToJS(*reinterpret_cast<const QVariantList *>(data));
        break;
    case QMetaType::QVariantMap:
        result = variantMapToJS(*reinterpret_cast<const QVariantMap *>(data));
        break;
    case QMetaType::QDateTime:
        result = QJSConverter::toDateTime(*reinterpret_cast<const QDateTime *>(data))->v4Value();
        break;
    case QMetaType::QDate:
        result = QJSConverter::toDateTime(QDateTime(*reinterpret_cast<const QDate *>(data)))->v4Value();
        break;
    case QMetaType::QRegExp:
        result = QJSConverter::toRegExp(*reinterpret_cast<const QRegExp *>(data))->v4Value();
        break;
    case QMetaType::QObjectStar:
        result = newQObject(*reinterpret_cast<QObject* const *>(data))->v4Value();
        break;
    case QMetaType::QVariant:
        result = variantToJS(*reinterpret_cast<const QVariant*>(data));
        break;
    case QMetaType::QJsonValue:
        result = m_jsonWrapper.fromJsonValue(*reinterpret_cast<const QJsonValue *>(data));
        break;
    case QMetaType::QJsonObject:
        result = m_jsonWrapper.fromJsonObject(*reinterpret_cast<const QJsonObject *>(data));
        break;
    case QMetaType::QJsonArray:
        result = m_jsonWrapper.fromJsonArray(*reinterpret_cast<const QJsonArray *>(data));
        break;
    default:
        if (type == qMetaTypeId<QJSValue>()) {
            return QJSValuePrivate::get(*reinterpret_cast<const QJSValue*>(data))->value;
        } else {
            QByteArray typeName = QMetaType::typeName(type);
            if (typeName.endsWith('*') && !*reinterpret_cast<void* const *>(data)) {
                return QV4::Value::nullValue();
            } else {
                // Fall back to wrapping in a QVariant.
                result = newVariant(QVariant(type, data))->v4Value();
            }
        }
    }
    return result;
}

// Converts a JS value to a meta-type.
// data must point to a place that can store a value of the given type.
// Returns true if conversion succeeded, false otherwise.
bool QV8Engine::metaTypeFromJS(const QV4::Value &value, int type, void *data) {
    // check if it's one of the types we know
    switch (QMetaType::Type(type)) {
    case QMetaType::Bool:
        *reinterpret_cast<bool*>(data) = value.toBoolean();
        return true;
    case QMetaType::Int:
        *reinterpret_cast<int*>(data) = value.toInt32();
        return true;
    case QMetaType::UInt:
        *reinterpret_cast<uint*>(data) = value.toUInt32();
        return true;
    case QMetaType::LongLong:
        *reinterpret_cast<qlonglong*>(data) = qlonglong(value.toInteger());
        return true;
    case QMetaType::ULongLong:
        *reinterpret_cast<qulonglong*>(data) = qulonglong(value.toInteger());
        return true;
    case QMetaType::Double:
        *reinterpret_cast<double*>(data) = value.toNumber();
        return true;
    case QMetaType::QString:
        if (value.isUndefined() || value.isNull())
            *reinterpret_cast<QString*>(data) = QString();
        else
            *reinterpret_cast<QString*>(data) = value.toString(m_v4Engine->current)->toQString();
        return true;
    case QMetaType::Float:
        *reinterpret_cast<float*>(data) = value.toNumber();
        return true;
    case QMetaType::Short:
        *reinterpret_cast<short*>(data) = short(value.toInt32());
        return true;
    case QMetaType::UShort:
        *reinterpret_cast<unsigned short*>(data) = value.toUInt16();
        return true;
    case QMetaType::Char:
        *reinterpret_cast<char*>(data) = char(value.toInt32());
        return true;
    case QMetaType::UChar:
        *reinterpret_cast<unsigned char*>(data) = (unsigned char)(value.toInt32());
        return true;
    case QMetaType::QChar:
        if (value.isString()) {
            QString str = value.stringValue()->toQString();
            *reinterpret_cast<QChar*>(data) = str.isEmpty() ? QChar() : str.at(0);
        } else {
            *reinterpret_cast<QChar*>(data) = QChar(ushort(value.toUInt16()));
        }
        return true;
    case QMetaType::QDateTime:
        if (QV4::DateObject *d = value.asDateObject()) {
            *reinterpret_cast<QDateTime *>(data) = QV4::DatePrototype::toQDateTime(d->value.doubleValue());
            return true;
        } break;
    case QMetaType::QDate:
        if (QV4::DateObject *d = value.asDateObject()) {
            *reinterpret_cast<QDate *>(data) = QV4::DatePrototype::toQDateTime(d->value.doubleValue()).date();
            return true;
        } break;
    case QMetaType::QRegExp:
        if (QV4::RegExpObject *r = value.asRegExpObject()) {
            *reinterpret_cast<QRegExp *>(data) = QJSConverter::toRegExp(v8::Handle<v8::RegExp>::Cast(v8::Value::fromV4Value(value)));
            return true;
        } break;
    case QMetaType::QObjectStar: {
        v8::Handle<v8::Value> v = v8::Value::fromV4Value(value);
        if (isQObject(v) || value.isNull()) {
            *reinterpret_cast<QObject* *>(data) = qtObjectFromJS(v);
            return true;
        } break;
    }
    case QMetaType::QStringList:
        if (QV4::ArrayObject *a = value.asArrayObject()) {
            *reinterpret_cast<QStringList *>(data) = QJSConverter::toStringList(v8::Handle<v8::Array>::Cast(v8::Value::fromV4Value(value)));
            return true;
        } break;
    case QMetaType::QVariantList:
        if (QV4::ArrayObject *a = value.asArrayObject()) {
            *reinterpret_cast<QVariantList *>(data) = variantListFromJS(a);
            return true;
        } break;
    case QMetaType::QVariantMap:
        if (QV4::Object *o = value.asObject()) {
            *reinterpret_cast<QVariantMap *>(data) = variantMapFromJS(o);
            return true;
        } break;
    case QMetaType::QVariant:
        *reinterpret_cast<QVariant*>(data) = variantFromJS(value);
        return true;
    case QMetaType::QJsonValue:
        *reinterpret_cast<QJsonValue *>(data) = jsonValueFromJS(v8::Value::fromV4Value(value));
        return true;
    case QMetaType::QJsonObject:
        *reinterpret_cast<QJsonObject *>(data) = jsonObjectFromJS(v8::Value::fromV4Value(value));
        return true;
    case QMetaType::QJsonArray:
        *reinterpret_cast<QJsonArray *>(data) = jsonArrayFromJS(v8::Value::fromV4Value(value));
        return true;
    default:
    ;
    }

#if 0
    if (isQtVariant(value)) {
        const QVariant &var = variantValue(value);
        // ### Enable once constructInPlace() is in qt master.
        if (var.userType() == type) {
            QMetaType::constructInPlace(type, data, var.constData());
            return true;
        }
        if (var.canConvert(type)) {
            QVariant vv = var;
            vv.convert(type);
            Q_ASSERT(vv.userType() == type);
            QMetaType::constructInPlace(type, data, vv.constData());
            return true;
        }

    }
#endif

    // Try to use magic; for compatibility with qscriptvalue_cast.

    QByteArray name = QMetaType::typeName(type);
    if (convertToNativeQObject(v8::Value::fromV4Value(value), name, reinterpret_cast<void* *>(data)))
        return true;
    if (isVariant(v8::Value::fromV4Value(value)) && name.endsWith('*')) {
        int valueType = QMetaType::type(name.left(name.size()-1));
        QVariant &var = variantValue(v8::Value::fromV4Value(value));
        if (valueType == var.userType()) {
            // We have T t, T* is requested, so return &t.
            *reinterpret_cast<void* *>(data) = var.data();
            return true;
        } else {
            // Look in the prototype chain.
            v8::Handle<v8::Value> proto = v8::Value::fromV4Value(value)->ToObject()->GetPrototype();
            while (proto->IsObject()) {
                bool canCast = false;
                if (isVariant(proto)) {
                    canCast = (type == variantValue(proto).userType())
                              || (valueType && (valueType == variantValue(proto).userType()));
                }
                else if (isQObject(proto)) {
                    QByteArray className = name.left(name.size()-1);
                    if (QObject *qobject = qtObjectFromJS(proto))
                        canCast = qobject->qt_metacast(className) != 0;
                }
                if (canCast) {
                    QByteArray varTypeName = QMetaType::typeName(var.userType());
                    if (varTypeName.endsWith('*'))
                        *reinterpret_cast<void* *>(data) = *reinterpret_cast<void* *>(var.data());
                    else
                        *reinterpret_cast<void* *>(data) = var.data();
                    return true;
                }
                proto = proto->ToObject()->GetPrototype();
            }
        }
    } else if (value.isNull() && name.endsWith('*')) {
        *reinterpret_cast<void* *>(data) = 0;
        return true;
    } else if (type == qMetaTypeId<QJSValue>()) {
        *reinterpret_cast<QJSValue*>(data) = QJSValuePrivate::get(new QJSValuePrivate(m_v4Engine, value));
        return true;
    }

    return false;
}

// Converts a QVariant to JS.
QV4::Value QV8Engine::variantToJS(const QVariant &value)
{
    return metaTypeToJS(value.userType(), value.constData());
}

// Converts a JS value to a QVariant.
// Undefined -> QVariant() (invalid)
// Null -> QVariant((void*)0)
// Boolean -> QVariant(bool)
// Number -> QVariant(double)
// String -> QVariant(QString)
// Array -> QVariantList(...)
// Date -> QVariant(QDateTime)
// RegExp -> QVariant(QRegExp)
// [Any other object] -> QVariantMap(...)
QVariant QV8Engine::variantFromJS(const QV4::Value &value,
                                  V8ObjectSet &visitedObjects)
{
    Q_ASSERT(!value.isDeleted());
    if (value.isUndefined())
        return QVariant();
    if (value.isNull())
        return QVariant(QMetaType::VoidStar, 0);
    if (value.isBoolean())
        return value.booleanValue();
    if (value.isInteger())
        return value.integerValue();
    if (value.isNumber())
        return value.asDouble();
    if (value.isString())
        return value.stringValue()->toQString();
    Q_ASSERT(value.isObject());
    if (QV4::ArrayObject *a = value.asArrayObject())
        return variantListFromJS(a, visitedObjects);
    if (QV4::DateObject *d = value.asDateObject())
        return QJSConverter::toDateTime(v8::Handle<v8::Date>::Cast(v8::Value::fromV4Value(value)));
    if (value.asRegExpObject())
        return QJSConverter::toRegExp(v8::Handle<v8::RegExp>::Cast(v8::Value::fromV4Value(value)));
    if (isVariant(v8::Value::fromV4Value(value)))
        return variantValue(v8::Value::fromV4Value(value));
    if (isQObject(v8::Value::fromV4Value(value)))
        return qVariantFromValue(qtObjectFromJS(v8::Value::fromV4Value(value)));
    if (isValueType(v8::Value::fromV4Value(value)))
        return toValueType(v8::Value::fromV4Value(value));
    return variantMapFromJS(value.asObject(), visitedObjects);
}

v8::Handle<v8::Value> QV8Engine::jsonValueToJS(const QJsonValue &value)
{
    return v8::Value::fromV4Value(m_jsonWrapper.fromJsonValue(value));
}

QJsonValue QV8Engine::jsonValueFromJS(v8::Handle<v8::Value> value)
{
    return m_jsonWrapper.toJsonValue(value.get()->v4Value());
}

v8::Local<v8::Object> QV8Engine::jsonObjectToJS(const QJsonObject &object)
{
    return v8::Local<v8::Object>::New(v8::Value::fromV4Value(m_jsonWrapper.fromJsonObject(object)));
}

QJsonObject QV8Engine::jsonObjectFromJS(v8::Handle<v8::Value> value)
{
    return m_jsonWrapper.toJsonObject(value.get()->v4Value().asObject());
}

v8::Local<v8::Array> QV8Engine::jsonArrayToJS(const QJsonArray &array)
{
    return v8::Local<v8::Array>::New(v8::Value::fromV4Value(m_jsonWrapper.fromJsonArray(array)));
}

QJsonArray QV8Engine::jsonArrayFromJS(v8::Handle<v8::Value> value)
{
    return m_jsonWrapper.toJsonArray(value.get()->v4Value().asArrayObject());
}

bool QV8Engine::convertToNativeQObject(v8::Handle<v8::Value> value,
                                                  const QByteArray &targetType,
                                                  void **result)
{
    if (!targetType.endsWith('*'))
        return false;
    if (QObject *qobject = qtObjectFromJS(value)) {
        int start = targetType.startsWith("const ") ? 6 : 0;
        QByteArray className = targetType.mid(start, targetType.size()-start-1);
        if (void *instance = qobject->qt_metacast(className)) {
            *result = instance;
            return true;
        }
    }
    return false;
}

QObject *QV8Engine::qtObjectFromJS(v8::Handle<v8::Value> value)
{
    if (!value->IsObject())
        return 0;

    QV8ObjectResource *r = (QV8ObjectResource *)value->ToObject()->GetExternalResource();
    if (!r)
        return 0;
    QV8ObjectResource::ResourceType type = r->resourceType();
    if (type == QV8ObjectResource::QObjectType)
        return qobjectWrapper()->toQObject(r);
    else if (type == QV8ObjectResource::VariantType) {
        QVariant variant = variantWrapper()->toVariant(r);
        int type = variant.userType();
        if (type == QMetaType::QObjectStar)
            return *reinterpret_cast<QObject* const *>(variant.constData());
    }
    return 0;
}


QVariant &QV8Engine::variantValue(v8::Handle<v8::Value> value)
{
    return variantWrapper()->variantValue(value);
}

// Creates a QVariant wrapper object.
v8::Local<v8::Object> QV8Engine::newVariant(const QVariant &value)
{
    return variantWrapper()->newVariant(value);
}

QJSValue QV8Engine::evaluate(v8::Handle<v8::Script> script, v8::TryCatch& tryCatch)
{
    v8::HandleScope handleScope;

    if (script.IsEmpty()) {
        v8::Handle<v8::Value> exception = tryCatch.Exception();
        if (exception.IsEmpty()) {
            // This is possible on syntax errors like { a:12, b:21 } <- missing "(", ")" around expression.
            return QJSValue();
        }
        return new QJSValuePrivate(m_v4Engine, exception.get()->v4Value());
    }
    v8::Handle<v8::Value> result;
    result = script->Run();
    if (result.IsEmpty()) {
        v8::Handle<v8::Value> exception = tryCatch.Exception();
        // TODO: figure out why v8 doesn't always produce an exception value
        //Q_ASSERT(!exception.IsEmpty());
        if (exception.IsEmpty())
            exception = v8::Exception::Error(v8::String::New("missing exception value"));
        return new QJSValuePrivate(m_v4Engine, exception.get()->v4Value());
    }
    return new QJSValuePrivate(m_v4Engine, result.get()->v4Value());
}

QJSValue QV8Engine::scriptValueFromInternal(v8::Handle<v8::Value> value) const
{
    if (value.IsEmpty())
        return QJSValue();
    return new QJSValuePrivate(m_v4Engine, value.get()->v4Value());
}

QJSValue QV8Engine::newArray(uint length)
{
    return new QJSValuePrivate(m_v4Engine, v8::Array::New(length).get()->v4Value());
}

void QV8Engine::startTimer(const QString &timerName)
{
    if (!m_time.isValid())
        m_time.start();
    m_startedTimers[timerName] = m_time.elapsed();
}

qint64 QV8Engine::stopTimer(const QString &timerName, bool *wasRunning)
{
    if (!m_startedTimers.contains(timerName)) {
        *wasRunning = false;
        return 0;
    }
    *wasRunning = true;
    qint64 startedAt = m_startedTimers.take(timerName);
    return m_time.elapsed() - startedAt;
}

int QV8Engine::consoleCountHelper(const QString &file, quint16 line, quint16 column)
{
    const QString key = file + QString::number(line) + QString::number(column);
    int number = m_consoleCount.value(key, 0);
    number++;
    m_consoleCount.insert(key, number);
    return number;
}

v8::Handle<v8::Value> QV8Engine::getPlatform(v8::Local<v8::String>, const v8::AccessorInfo &info)
{
    QV8Engine *engine = reinterpret_cast<QV8Engine*>(v8::External::Cast(info.Data().get())->Value());
    if (!engine->m_platform) {
        // Only allocate a platform object once
        engine->m_platform = new QQmlPlatform(engine->m_engine);
    }
    return engine->newQObject(engine->m_platform);
}

v8::Handle<v8::Value> QV8Engine::getApplication(v8::Local<v8::String>, const v8::AccessorInfo &info)
{
    QV8Engine *engine = reinterpret_cast<QV8Engine*>(v8::External::Cast(info.Data().get())->Value());
    if (!engine->m_application) {
        // Only allocate an application object once
        engine->m_application = QQml_guiProvider()->application(engine->m_engine);
    }
    return engine->newQObject(engine->m_application);
}

#ifndef QT_NO_IM
v8::Handle<v8::Value> QV8Engine::getInputMethod(v8::Local<v8::String>, const v8::AccessorInfo &info)
{
    QV8Engine *engine = reinterpret_cast<QV8Engine*>(v8::External::Cast(info.Data().get())->Value());
    return engine->newQObject(QQml_guiProvider()->inputMethod(), CppOwnership);
}
#endif

void QV8GCCallback::registerGcPrologueCallback()
{
    QV8Engine::ThreadData *td = QV8Engine::threadData();
    if (!td->gcPrologueCallbackRegistered) {
        td->gcPrologueCallbackRegistered = true;
        v8::V8::AddGCPrologueCallback(QV8GCCallback::garbageCollectorPrologueCallback, v8::kGCTypeMarkSweepCompact);
    }
}

QV8GCCallback::Node::Node(PrologueCallback callback)
    : prologueCallback(callback)
{
}

QV8GCCallback::Node::~Node()
{
    node.remove();
}

/*
   Ensure that each persistent handle is strong if it has CPP ownership
   and has no implicitly JS owned object owner in its parent chain, and
   weak otherwise.

   Any weak handle whose parent object is still alive will have an implicit
   reference (between the parent and the handle) added, so that it will
   not be collected.

   Note that this callback is registered only for kGCTypeMarkSweepCompact
   collection cycles, as it is during collection cycles of that type
   in which weak persistent handle callbacks are called when required.
 */
void QV8GCCallback::garbageCollectorPrologueCallback(v8::GCType, v8::GCCallbackFlags)
{
    if (!QV8Engine::hasThreadData())
        return;

    QV8Engine::ThreadData *td = QV8Engine::threadData();
    QV8GCCallback::Node *currNode = td->gcCallbackNodes.first();

    while (currNode) {
        // The client which adds itself to the list is responsible
        // for maintaining the correct implicit references in the
        // specified callback.
        currNode->prologueCallback(currNode);
        currNode = td->gcCallbackNodes.next(currNode);
    }
}

void QV8GCCallback::addGcCallbackNode(QV8GCCallback::Node *node)
{
    QV8Engine::ThreadData *td = QV8Engine::threadData();
    td->gcCallbackNodes.insert(node);
}

QV8Engine::ThreadData::ThreadData()
    : gcPrologueCallbackRegistered(false)
{
    if (!v8::Isolate::GetCurrent()) {
        isolate = v8::Isolate::New();
        isolate->Enter();
    } else {
        isolate = 0;
    }
}

QV8Engine::ThreadData::~ThreadData()
{
    if (isolate) {
        isolate->Exit();
        isolate->Dispose();
        isolate = 0;
    }
}

QString QV8Engine::toString(v8::Handle<v8::Value> string)
{
    return string.get()->v4Value().toString(m_v4Engine->current)->toQString();
}

QString QV8Engine::toString(v8::Handle<v8::String> string)
{
    return string.get()->v4Value().toString(m_v4Engine->current)->toQString();
}

v8::Local<v8::String> QV8Engine::toString(const QString &string)
{
    return QJSConverter::toString(string);
}


QV4::Value QQmlV4Handle::toValue() const
{
    QV4::Value val;
    val.val = d;
    return val;
}

QQmlV4Handle QQmlV4Handle::fromValue(const QV4::Value &v)
{
    QQmlV4Handle handle;
    handle.d = v.val;
    return handle;
}

QJSValue QV8Engine::evaluate(const QString& program, const QString& fileName, quint16 lineNumber)
{
    v8::TryCatch tryCatch;
    v8::ScriptOrigin scriptOrigin(QJSConverter::toString(fileName), v8::Integer::New(lineNumber - 1));
    v8::Handle<v8::Script> script;
    script = v8::Script::Compile(QJSConverter::toString(program), &scriptOrigin);
    if (script.IsEmpty()) {
        // TODO: Why don't we get the exception, as with Script::Compile()?
        // Q_ASSERT(tryCatch.HasCaught());
        QV4::Object *error = m_v4Engine->newSyntaxErrorObject(m_v4Engine->current, 0);
        return new QJSValuePrivate(m_v4Engine, QV4::Value::fromObject(error));
    }
    return evaluate(script, tryCatch);
}

QV4::Value QV8Engine::evaluateScript(const QString &script, QV4::Object *scopeObject)
{
    QV4::ExecutionContext *ctx = m_v4Engine->current;

    QV4::Value result = QV4::Value::undefinedValue();

    try {
        QV4::EvalFunction *eval = new (m_v4Engine->memoryManager) QV4::EvalFunction(m_v4Engine->rootContext, scopeObject);
        QV4::Value arg = QV4::Value::fromString(m_v4Engine->current, script);
        result = eval->evalCall(m_v4Engine->current, QV4::Value::undefinedValue(), &arg, 1, /*directCall*/ false);
    } catch (QV4::Exception &e) {
        e.accept(ctx);
    }
    return result;
}


QT_END_NAMESPACE

