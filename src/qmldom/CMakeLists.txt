# Generated from qmldom.pro.

#####################################################################
## QmlDomPrivate Module:
#####################################################################

qt_internal_add_module(QmlDomPrivate
    STATIC
    INTERNAL_MODULE
    # NO_SYNC_QT # special case remove, it's actually needed for tests
    SOURCES
        qqmldom_fwd_p.h
        qqmldom_global.h
        qqmldomastcreator.cpp qqmldomastcreator_p.h
        qqmldomastdumper.cpp qqmldomastdumper_p.h
        qqmldomattachedinfo.cpp qqmldomattachedinfo_p.h
        qqmldomcomments.cpp qqmldomcomments_p.h
        qqmldomcompare.cpp qqmldomcompare_p.h
        qqmldomconstants.cpp qqmldomconstants_p.h
        qqmldomelements.cpp qqmldomelements_p.h
        qqmldomerrormessage.cpp qqmldomerrormessage_p.h
        qqmldomexternalitems.cpp qqmldomexternalitems_p.h
        qqmldomfieldfilter.cpp qqmldomfieldfilter_p.h
        qqmldomfilewriter.cpp qqmldomfilewriter_p.h
        qqmldomfunctionref_p.h
        qqmldomitem.cpp qqmldomitem_p.h
        qqmldommock.cpp qqmldommock_p.h
        qqmldomlinewriter.cpp qqmldomlinewriter_p.h
        qqmldommoduleindex.cpp qqmldommoduleindex_p.h
        qqmldomoutwriter.cpp qqmldomoutwriter_p.h
        qqmldompath.cpp qqmldompath_p.h
        qqmldomstringdumper.cpp qqmldomstringdumper_p.h
        qqmldomreformatter.cpp qqmldomreformatter_p.h
        qqmldomtop.cpp qqmldomtop_p.h
        qqmldomtypesreader.cpp qqmldomtypesreader_p.h
    DEFINES
        QMLDOM_LIBRARY
    PUBLIC_LIBRARIES
        Qt::QmlPrivate
        Qt::QmlCompilerPrivate
)

#### Keys ignored in scope 1:.:.:qmldom.pro:<TRUE>:
# _OPTION = "host_build"
