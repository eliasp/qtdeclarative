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

#include "qv4object_p.h"
#include "qv4jsir_p.h"
#include "qv4isel_p.h"
#include "qv4objectproto_p.h"
#include "qv4stringobject_p.h"
#include "qv4function_p.h"
#include "qv4mm_p.h"
#include "qv4exception_p.h"
#include "qv4arrayobject_p.h"
#include "qv4scopedvalue_p.h"

#include <private/qqmljsengine_p.h>
#include <private/qqmljslexer_p.h>
#include <private/qqmljsparser_p.h>
#include <private/qqmljsast_p.h>
#include <qv4jsir_p.h>
#include <qv4codegen_p.h>
#include "private/qlocale_tools_p.h"

#include <QtCore/qmath.h>
#include <QtCore/QDebug>
#include <cassert>
#include <typeinfo>
#include <iostream>
#include <algorithm>
#include "qv4alloca_p.h"

using namespace QV4;


DEFINE_MANAGED_VTABLE(FunctionObject);

FunctionObject::FunctionObject(ExecutionContext *scope, const StringRef name, bool createProto)
    : Object(createProto ? scope->engine->functionWithProtoClass : scope->engine->functionClass)
    , scope(scope)
    , formalParameterList(0)
    , varList(0)
    , formalParameterCount(0)
    , varCount(0)
    , function(0)
{
     init(name, createProto);
}

FunctionObject::FunctionObject(ExecutionContext *scope, const QString &name, bool createProto)
    : Object(createProto ? scope->engine->functionWithProtoClass : scope->engine->functionClass)
    , scope(scope)
    , formalParameterList(0)
    , varList(0)
    , formalParameterCount(0)
    , varCount(0)
    , function(0)
{
    Scope s(scope);
    ScopedValue protectThis(s, this);
    ScopedString n(s, s.engine->newString(name));
    init(n, createProto);
}

FunctionObject::FunctionObject(InternalClass *ic)
    : Object(ic)
    , scope(ic->engine->rootContext)
    , formalParameterList(0)
    , varList(0)
    , formalParameterCount(0)
    , varCount(0)
    , function(0)
{
    vtbl = &static_vtbl;
    name = (QV4::String *)0;

    type = Type_FunctionObject;
    needsActivation = false;
    usesArgumentsObject = false;
    strictMode = false;
}

FunctionObject::~FunctionObject()
{
    if (function)
        function->compilationUnit->deref();
}

void FunctionObject::init(const StringRef n, bool createProto)
{
    vtbl = &static_vtbl;

    Scope s(engine());
    ScopedValue protectThis(s, this);

    type = Type_FunctionObject;
    needsActivation = true;
    usesArgumentsObject = false;
    strictMode = false;
#ifndef QT_NO_DEBUG
     assert(scope->next != (ExecutionContext *)0x1);
#endif

    if (createProto) {
        Scoped<Object> proto(s, scope->engine->newObject(scope->engine->protoClass));
        proto->memberData[Index_ProtoConstructor].value = Value::fromObject(this);
        memberData[Index_Prototype].value = proto.asValue();
    }

    if (n) {
        name = n;
        ScopedValue v(s, n.asReturnedValue());
        defineReadonlyProperty(scope->engine->id_name, v);
    } else {
        name = (QV4::String *)0;
    }
}

ReturnedValue FunctionObject::newInstance()
{
    Scope scope(engine());
    ScopedCallData callData(scope, 0);
    return construct(callData);
}

bool FunctionObject::hasInstance(Managed *that, const ValueRef value)
{
    Scope scope(that->engine());
    ScopedFunctionObject f(scope, static_cast<FunctionObject *>(that));

    ScopedObject v(scope, value);
    if (!v)
        return false;

    Scoped<Object> o(scope, f->get(scope.engine->id_prototype));
    if (!o)
        scope.engine->current->throwTypeError();

    while (v) {
        v = v->prototype();

        if (! v)
            break;
        else if (o.getPointer() == v)
            return true;
    }

    return false;
}

ReturnedValue FunctionObject::construct(Managed *that, CallData *)
{
    ExecutionEngine *v4 = that->engine();
    Scope scope(v4);
    Scoped<FunctionObject> f(scope, that, Scoped<FunctionObject>::Cast);

    InternalClass *ic = v4->objectClass;
    Scoped<Object> proto(scope, f->get(v4->id_prototype));
    if (!!proto)
        ic = v4->emptyClass->changePrototype(proto.getPointer());
    Scoped<Object> obj(scope, v4->newObject(ic));
    return obj.asReturnedValue();
}

ReturnedValue FunctionObject::call(Managed *, CallData *)
{
    return Encode::undefined();
}

void FunctionObject::markObjects(Managed *that)
{
    FunctionObject *o = static_cast<FunctionObject *>(that);
    if (o->name.managed())
        o->name->mark();
    // these are marked in VM::Function:
//    for (uint i = 0; i < formalParameterCount; ++i)
//        formalParameterList[i]->mark();
//    for (uint i = 0; i < varCount; ++i)
//        varList[i]->mark();
    o->scope->mark();
    if (o->function)
        o->function->mark();

    Object::markObjects(that);
}

FunctionObject *FunctionObject::creatScriptFunction(ExecutionContext *scope, Function *function)
{
    if (function->needsActivation() || function->compiledFunction->nFormals > QV4::Global::ReservedArgumentCount)
        return new (scope->engine->memoryManager) ScriptFunction(scope, function);
    return new (scope->engine->memoryManager) SimpleScriptFunction(scope, function);
}


DEFINE_MANAGED_VTABLE(FunctionCtor);

FunctionCtor::FunctionCtor(ExecutionContext *scope)
    : FunctionObject(scope, QStringLiteral("Function"))
{
    vtbl = &static_vtbl;
}

// 15.3.2
ReturnedValue FunctionCtor::construct(Managed *that, CallData *callData)
{
    FunctionCtor *f = static_cast<FunctionCtor *>(that);
    ExecutionContext *ctx = f->engine()->current;
    QString arguments;
    QString body;
    if (callData->argc > 0) {
        for (uint i = 0; i < callData->argc - 1; ++i) {
            if (i)
                arguments += QLatin1String(", ");
            arguments += callData->args[i].toString(ctx)->toQString();
        }
        body = callData->args[callData->argc - 1].toString(ctx)->toQString();
    }

    QString function = QLatin1String("function(") + arguments + QLatin1String("){") + body + QLatin1String("}");

    QQmlJS::Engine ee, *engine = &ee;
    QQmlJS::Lexer lexer(engine);
    lexer.setCode(function, 1, false);
    QQmlJS::Parser parser(engine);

    const bool parsed = parser.parseExpression();

    if (!parsed)
        f->engine()->current->throwSyntaxError(0);

    using namespace QQmlJS::AST;
    FunctionExpression *fe = QQmlJS::AST::cast<FunctionExpression *>(parser.rootNode());
    ExecutionEngine *v4 = f->engine();
    if (!fe)
        v4->current->throwSyntaxError(0);

    QQmlJS::V4IR::Module module;

    QQmlJS::RuntimeCodegen cg(v4->current, f->strictMode);
    cg.generateFromFunctionExpression(QString(), function, fe, &module);

    QV4::Compiler::JSUnitGenerator jsGenerator(&module);
    QScopedPointer<QQmlJS::EvalInstructionSelection> isel(v4->iselFactory->create(v4->executableAllocator, &module, &jsGenerator));
    QV4::CompiledData::CompilationUnit *compilationUnit = isel->compile();
    QV4::Function *vmf = compilationUnit->linkToEngine(v4);

    return Value::fromObject(FunctionObject::creatScriptFunction(v4->rootContext, vmf)).asReturnedValue();
}

// 15.3.1: This is equivalent to new Function(...)
ReturnedValue FunctionCtor::call(Managed *that, CallData *callData)
{
    return construct(that, callData);
}

FunctionPrototype::FunctionPrototype(InternalClass *ic)
    : FunctionObject(ic)
{
}

void FunctionPrototype::init(ExecutionEngine *engine, const Value &ctor)
{
    ctor.objectValue()->defineReadonlyProperty(engine->id_length, Value::fromInt32(1));
    ctor.objectValue()->defineReadonlyProperty(engine->id_prototype, Value::fromObject(this));

    defineReadonlyProperty(engine->id_length, Value::fromInt32(0));
    defineDefaultProperty(QStringLiteral("constructor"), ctor);
    defineDefaultProperty(engine->id_toString, method_toString, 0);
    defineDefaultProperty(QStringLiteral("apply"), method_apply, 2);
    defineDefaultProperty(QStringLiteral("call"), method_call, 1);
    defineDefaultProperty(QStringLiteral("bind"), method_bind, 1);

}

ReturnedValue FunctionPrototype::method_toString(SimpleCallContext *ctx)
{
    FunctionObject *fun = ctx->thisObject.asFunctionObject();
    if (!fun)
        ctx->throwTypeError();

    return Value::fromString(ctx, QStringLiteral("function() { [code] }")).asReturnedValue();
}

ReturnedValue FunctionPrototype::method_apply(SimpleCallContext *ctx)
{
    Scope scope(ctx);
    FunctionObject *o = ctx->thisObject.asFunctionObject();
    if (!o)
        ctx->throwTypeError();

    ScopedValue arg(scope, ctx->argument(1));

    Scoped<Object> arr(scope, arg);

    quint32 len;
    if (!arr) {
        len = 0;
        if (!arg->isNullOrUndefined()) {
            ctx->throwTypeError();
            return Encode::undefined();
        }
    } else {
        len = ArrayPrototype::getLength(ctx, arr.getPointer());
    }

    ScopedCallData callData(scope, len);

    if (len) {
        if (arr->protoHasArray() || arr->hasAccessorProperty) {
            for (quint32 i = 0; i < len; ++i)
                callData->args[i] = Value::fromReturnedValue(arr->getIndexed(i));
        } else {
            int alen = qMin(len, arr->arrayDataLen);
            for (quint32 i = 0; i < alen; ++i)
                callData->args[i] = arr->arrayData[i].value;
            for (quint32 i = alen; i < len; ++i)
                callData->args[i] = Value::undefinedValue();
        }
    }

    callData->thisObject = ctx->argument(0);
    return o->call(callData);
}

ReturnedValue FunctionPrototype::method_call(SimpleCallContext *ctx)
{
    Scope scope(ctx);

    FunctionObject *o = ctx->thisObject.asFunctionObject();
    if (!o)
        ctx->throwTypeError();

    ScopedCallData callData(scope, ctx->argumentCount ? ctx->argumentCount - 1 : 0);
    if (ctx->argumentCount) {
        std::copy(ctx->arguments + 1,
                  ctx->arguments + ctx->argumentCount, callData->args);
    }
    callData->thisObject = ctx->argument(0);
    return o->call(callData);
}

ReturnedValue FunctionPrototype::method_bind(SimpleCallContext *ctx)
{
    Scope scope(ctx);
    Scoped<FunctionObject> target(scope, ctx->thisObject);
    if (!target)
        ctx->throwTypeError();

    ScopedValue boundThis(scope, ctx->argument(0));
    QVector<Value> boundArgs;
    for (uint i = 1; i < ctx->argumentCount; ++i)
        boundArgs += ctx->arguments[i];

    return ctx->engine->newBoundFunction(ctx->engine->rootContext, target.getPointer(), boundThis, boundArgs)->asReturnedValue();
}

DEFINE_MANAGED_VTABLE(ScriptFunction);

ScriptFunction::ScriptFunction(ExecutionContext *scope, Function *function)
    : FunctionObject(scope, function->name, true)
{
    vtbl = &static_vtbl;

    Scope s(scope);
    ScopedValue protectThis(s, this);

    this->function = function;
    this->function->compilationUnit->ref();
    Q_ASSERT(function);
    Q_ASSERT(function->codePtr);

    // global function
    if (!scope)
        return;

    ExecutionEngine *v4 = scope->engine;

    needsActivation = function->needsActivation();
    usesArgumentsObject = function->usesArgumentsObject();
    strictMode = function->isStrict();
    formalParameterCount = function->formals.size();
    formalParameterList = function->formals.constData();
    defineReadonlyProperty(scope->engine->id_length, Value::fromInt32(formalParameterCount));

    varCount = function->locals.size();
    varList = function->locals.constData();

    if (scope->strictMode) {
        Property pd = Property::fromAccessor(v4->thrower, v4->thrower);
        *insertMember(scope->engine->id_caller, Attr_Accessor|Attr_NotConfigurable|Attr_NotEnumerable) = pd;
        *insertMember(scope->engine->id_arguments, Attr_Accessor|Attr_NotConfigurable|Attr_NotEnumerable) = pd;
    }
}

ReturnedValue ScriptFunction::construct(Managed *that, CallData *callData)
{
    ExecutionEngine *v4 = that->engine();
    Scope scope(v4);
    Scoped<ScriptFunction> f(scope, static_cast<ScriptFunction *>(that));

    InternalClass *ic = v4->objectClass;
    Value proto = f->memberData[Index_Prototype].value;
    if (proto.isObject())
        ic = v4->emptyClass->changePrototype(proto.objectValue());
    Scoped<Object> obj(scope, v4->newObject(ic));

    ExecutionContext *context = v4->current;
    callData->thisObject = obj.asValue();
    ExecutionContext *ctx = context->newCallContext(f.getPointer(), callData);

    ScopedValue result(scope);
    SAVE_JS_STACK(f->scope);
    try {
        result = f->function->code(ctx, f->function->codeData);
    } catch (Exception &ex) {
        ex.partiallyUnwindContext(context);
        throw;
    }
    CHECK_JS_STACK(f->scope);
    ctx->engine->popContext();

    if (result->isObject())
        return result.asReturnedValue();
    return obj.asReturnedValue();
}

ReturnedValue ScriptFunction::call(Managed *that, CallData *callData)
{
    ScriptFunction *f = static_cast<ScriptFunction *>(that);
    void *stackSpace;
    ExecutionContext *context = f->engine()->current;
    Scope scope(context);
    CallContext *ctx = context->newCallContext(f, callData);

    if (!f->strictMode && !callData->thisObject.isObject()) {
        if (callData->thisObject.isNullOrUndefined()) {
            ctx->thisObject = Value::fromObject(f->engine()->globalObject);
        } else {
            ctx->thisObject = Value::fromObject(callData->thisObject.toObject(context));
        }
    }

    ScopedValue result(scope);
    SAVE_JS_STACK(f->scope);
    try {
        result = f->function->code(ctx, f->function->codeData);
    } catch (Exception &ex) {
        ex.partiallyUnwindContext(context);
        throw;
    }
    CHECK_JS_STACK(f->scope);
    ctx->engine->popContext();
    return result.asReturnedValue();
}

DEFINE_MANAGED_VTABLE(SimpleScriptFunction);

SimpleScriptFunction::SimpleScriptFunction(ExecutionContext *scope, Function *function)
    : FunctionObject(scope, function->name, true)
{
    vtbl = &static_vtbl;

    Scope s(scope);
    ScopedValue protectThis(s, this);

    this->function = function;
    this->function->compilationUnit->ref();
    Q_ASSERT(function);
    Q_ASSERT(function->codePtr);

    // global function
    if (!scope)
        return;

    ExecutionEngine *v4 = scope->engine;

    needsActivation = function->needsActivation();
    usesArgumentsObject = function->usesArgumentsObject();
    strictMode = function->isStrict();
    formalParameterCount = function->formals.size();
    formalParameterList = function->formals.constData();
    defineReadonlyProperty(scope->engine->id_length, Value::fromInt32(formalParameterCount));

    varCount = function->locals.size();
    varList = function->locals.constData();

    if (scope->strictMode) {
        Property pd = Property::fromAccessor(v4->thrower, v4->thrower);
        *insertMember(scope->engine->id_caller, Attr_Accessor|Attr_NotConfigurable|Attr_NotEnumerable) = pd;
        *insertMember(scope->engine->id_arguments, Attr_Accessor|Attr_NotConfigurable|Attr_NotEnumerable) = pd;
    }
}

ReturnedValue SimpleScriptFunction::construct(Managed *that, CallData *callData)
{
    ExecutionEngine *v4 = that->engine();
    Scope scope(v4);
    Scoped<SimpleScriptFunction> f(scope, static_cast<SimpleScriptFunction *>(that));

    InternalClass *ic = v4->objectClass;
    Scoped<Object> proto(scope, f->memberData[Index_Prototype].value);
    if (!!proto)
        ic = v4->emptyClass->changePrototype(proto.getPointer());
    Scoped<Object> obj(scope, v4->newObject(ic));

    ExecutionContext *context = v4->current;
    void *stackSpace = alloca(requiredMemoryForExecutionContectSimple(f));
    callData->thisObject = obj;
    ExecutionContext *ctx = context->newCallContext(stackSpace, f.getPointer(), callData);

    try {
        Scoped<Object> result(scope, f->function->code(ctx, f->function->codeData));
        ctx->engine->popContext();

        if (!result)
            return obj.asReturnedValue();
        return result.asReturnedValue();
    } catch (Exception &ex) {
        ex.partiallyUnwindContext(context);
        throw;
    }
}

ReturnedValue SimpleScriptFunction::call(Managed *that, CallData *callData)
{
    SimpleScriptFunction *f = static_cast<SimpleScriptFunction *>(that);
    void *stackSpace = alloca(requiredMemoryForExecutionContectSimple(f));
    ExecutionContext *context = f->engine()->current;
    Scope scope(context);
    ExecutionContext *ctx = context->newCallContext(stackSpace, f, callData);

    if (!f->strictMode && !callData->thisObject.isObject()) {
        if (callData->thisObject.isNullOrUndefined()) {
            ctx->thisObject = Value::fromObject(f->engine()->globalObject);
        } else {
            ctx->thisObject = Value::fromObject(callData->thisObject.toObject(context));
        }
    }

    ScopedValue result(scope);
    SAVE_JS_STACK(f->scope);
    try {
        result = f->function->code(ctx, f->function->codeData);
    } catch (Exception &ex) {
        ex.partiallyUnwindContext(context);
        throw;
    }
    CHECK_JS_STACK(f->scope);
    ctx->engine->popContext();
    return result.asReturnedValue();
}




DEFINE_MANAGED_VTABLE(BuiltinFunction);

BuiltinFunction::BuiltinFunction(ExecutionContext *scope, const StringRef name, ReturnedValue (*code)(SimpleCallContext *))
    : FunctionObject(scope, name)
    , code(code)
{
    vtbl = &static_vtbl;
    isBuiltinFunction = true;
}

ReturnedValue BuiltinFunction::construct(Managed *f, CallData *)
{
    f->engine()->current->throwTypeError();
    return Encode::undefined();
}

ReturnedValue BuiltinFunction::call(Managed *that, CallData *callData)
{
    BuiltinFunction *f = static_cast<BuiltinFunction *>(that);
    ExecutionEngine *v4 = f->engine();
    Scope scope(v4);
    ExecutionContext *context = v4->current;

    SimpleCallContext ctx;
    ctx.initSimpleCallContext(f->scope->engine);
    ctx.strictMode = f->scope->strictMode; // ### needed? scope or parent context?
    ctx.thisObject = callData->thisObject;
    // ### const_cast
    ctx.arguments = const_cast<SafeValue *>(callData->args);
    ctx.argumentCount = callData->argc;
    v4->pushContext(&ctx);

    ScopedValue result(scope);
    try {
        result = f->code(&ctx);
    } catch (Exception &ex) {
        ex.partiallyUnwindContext(context);
        throw;
    }

    context->engine->popContext();
    return result.asReturnedValue();
}

ReturnedValue IndexedBuiltinFunction::call(Managed *that, CallData *callData)
{
    IndexedBuiltinFunction *f = static_cast<IndexedBuiltinFunction *>(that);
    ExecutionEngine *v4 = f->engine();
    ExecutionContext *context = v4->current;
    Scope scope(v4);

    SimpleCallContext ctx;
    ctx.initSimpleCallContext(f->scope->engine);
    ctx.strictMode = f->scope->strictMode; // ### needed? scope or parent context?
    ctx.thisObject = callData->thisObject;
    // ### const_cast
    ctx.arguments = const_cast<SafeValue *>(callData->args);
    ctx.argumentCount = callData->argc;
    v4->pushContext(&ctx);

    ScopedValue result(scope);
    try {
        result = f->code(&ctx, f->index);
    } catch (Exception &ex) {
        ex.partiallyUnwindContext(context);
        throw;
    }

    context->engine->popContext();
    return result.asReturnedValue();
}

DEFINE_MANAGED_VTABLE(IndexedBuiltinFunction);

DEFINE_MANAGED_VTABLE(BoundFunction);

BoundFunction::BoundFunction(ExecutionContext *scope, FunctionObject *target, Value boundThis, const QVector<Value> &boundArgs)
    : FunctionObject(scope, 0)
    , target(target)
    , boundThis(boundThis)
    , boundArgs(boundArgs)
{
    vtbl = &static_vtbl;

    Scope s(scope);
    ScopedValue protectThis(s, this);

    int len = Value::fromReturnedValue(target->get(scope->engine->id_length)).toUInt32();
    len -= boundArgs.size();
    if (len < 0)
        len = 0;
    defineReadonlyProperty(scope->engine->id_length, Value::fromInt32(len));

    ExecutionEngine *v4 = scope->engine;

    Property pd = Property::fromAccessor(v4->thrower, v4->thrower);
    *insertMember(scope->engine->id_arguments, Attr_Accessor|Attr_NotConfigurable|Attr_NotEnumerable) = pd;
    *insertMember(scope->engine->id_caller, Attr_Accessor|Attr_NotConfigurable|Attr_NotEnumerable) = pd;
}

void BoundFunction::destroy(Managed *that)
{
    static_cast<BoundFunction *>(that)->~BoundFunction();
}

ReturnedValue BoundFunction::call(Managed *that, CallData *dd)
{
    BoundFunction *f = static_cast<BoundFunction *>(that);
    Scope scope(f->scope->engine);

    ScopedCallData callData(scope, f->boundArgs.size() + dd->argc);
    callData->thisObject = f->boundThis;
    memcpy(callData->args, f->boundArgs.constData(), f->boundArgs.size()*sizeof(Value));
    memcpy(callData->args + f->boundArgs.size(), dd->args, dd->argc*sizeof(Value));
    return f->target->call(callData);
}

ReturnedValue BoundFunction::construct(Managed *that, CallData *dd)
{
    BoundFunction *f = static_cast<BoundFunction *>(that);
    Scope scope(f->scope->engine);
    ScopedCallData callData(scope, f->boundArgs.size() + dd->argc);
    memcpy(callData->args, f->boundArgs.constData(), f->boundArgs.size()*sizeof(Value));
    memcpy(callData->args + f->boundArgs.size(), dd->args, dd->argc*sizeof(Value));
    return f->target->construct(callData);
}

bool BoundFunction::hasInstance(Managed *that, const ValueRef value)
{
    BoundFunction *f = static_cast<BoundFunction *>(that);
    return FunctionObject::hasInstance(f->target, value);
}

void BoundFunction::markObjects(Managed *that)
{
    BoundFunction *o = static_cast<BoundFunction *>(that);
    o->target->mark();
    if (Managed *m = o->boundThis.asManaged())
        m->mark();
    for (int i = 0; i < o->boundArgs.size(); ++i)
        if (Managed *m = o->boundArgs.at(i).asManaged())
            m->mark();
    FunctionObject::markObjects(that);
}
