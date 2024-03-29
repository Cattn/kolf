cmake_minimum_required (VERSION 3.16 FATAL_ERROR)

# KDE Application Version, managed by release script
set(RELEASE_SERVICE_VERSION_MAJOR "23")
set(RELEASE_SERVICE_VERSION_MINOR "08")
set(RELEASE_SERVICE_VERSION_MICRO "4")
set(RELEASE_SERVICE_VERSION "${RELEASE_SERVICE_VERSION_MAJOR}.${RELEASE_SERVICE_VERSION_MINOR}.${RELEASE_SERVICE_VERSION_MICRO}")

project(kolf VERSION ${RELEASE_SERVICE_VERSION})



set (QT_MIN_VERSION "5.15.0")
set (KF_MIN_VERSION "5.92.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED CONFIG)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

include(ECMAddAppIcon)
include(ECMInstallIcons)
include(ECMSetupVersion)
include(FeatureSummary)
include(ECMDeprecationSettings)
if (QT_MAJOR_VERSION STREQUAL "6")
    set(QT_REQUIRED_VERSION "6.4.0")
    set(KF_MIN_VERSION "5.240.0")
    set(KF_MAJOR_VERSION "6")
else()
    set(KF_MAJOR_VERSION "5")
endif()

find_package(Qt${QT_MAJOR_VERSION} ${QT_MIN_VERSION} REQUIRED NO_MODULE COMPONENTS Gui Widgets)

find_package(KF${KF_MAJOR_VERSION} ${KF_MIN_VERSION} REQUIRED COMPONENTS
    Completion
    Config
    ConfigWidgets
    CoreAddons
    Crash
    DBusAddons
    DocTools
    I18n
    KIO
    TextWidgets
    WidgetsAddons
    XmlGui
)

if (QT_MAJOR_VERSION STREQUAL "6")
    find_package(KDEGames6 7.5.0 REQUIRED)
else()
    find_package(KF5KDEGames 7.3.0 REQUIRED)
endif()

ecm_setup_version(${RELEASE_SERVICE_VERSION} VARIABLE_PREFIX KOLF VERSION_HEADER kolf_version.h)

ecm_set_disabled_deprecation_versions(
    QT 5.15.2
    KF 5.103
    KDEGAMES 7.3
)


add_subdirectory(sounds)
add_subdirectory(courses)
add_subdirectory(pics)
add_subdirectory(graphics)
add_subdirectory(doc)

add_library(kolfprivate STATIC)

# we have a static copy of Box2D in our source tree
target_include_directories(kolfprivate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external)

target_sources(kolfprivate PRIVATE
    external/Box2D/Collision/b2BroadPhase.cpp
    external/Box2D/Collision/b2BroadPhase.h
    external/Box2D/Collision/b2CollideCircle.cpp
    external/Box2D/Collision/b2CollideEdge.cpp
    external/Box2D/Collision/b2CollidePolygon.cpp
    external/Box2D/Collision/b2Collision.cpp
    external/Box2D/Collision/b2Collision.h
    external/Box2D/Collision/b2Distance.cpp
    external/Box2D/Collision/b2Distance.h
    external/Box2D/Collision/b2DynamicTree.cpp
    external/Box2D/Collision/b2DynamicTree.h
    external/Box2D/Collision/b2TimeOfImpact.cpp
    external/Box2D/Collision/b2TimeOfImpact.h
    external/Box2D/Collision/Shapes/b2CircleShape.cpp
    external/Box2D/Collision/Shapes/b2CircleShape.h
    external/Box2D/Collision/Shapes/b2EdgeShape.cpp
    external/Box2D/Collision/Shapes/b2EdgeShape.h
    external/Box2D/Collision/Shapes/b2LoopShape.cpp
    external/Box2D/Collision/Shapes/b2LoopShape.h
    external/Box2D/Collision/Shapes/b2PolygonShape.cpp
    external/Box2D/Collision/Shapes/b2PolygonShape.h
    external/Box2D/Collision/Shapes/b2Shape.h
    external/Box2D/Common/b2BlockAllocator.cpp
    external/Box2D/Common/b2BlockAllocator.h
    external/Box2D/Common/b2GrowableStack.h
    external/Box2D/Common/b2Math.cpp
    external/Box2D/Common/b2Math.h
    external/Box2D/Common/b2Settings.cpp
    external/Box2D/Common/b2Settings.h
    external/Box2D/Common/b2StackAllocator.cpp
    external/Box2D/Common/b2StackAllocator.h
    external/Box2D/Dynamics/b2Body.cpp
    external/Box2D/Dynamics/b2Body.h
    external/Box2D/Dynamics/b2ContactManager.cpp
    external/Box2D/Dynamics/b2ContactManager.h
    external/Box2D/Dynamics/b2Fixture.cpp
    external/Box2D/Dynamics/b2Fixture.h
    external/Box2D/Dynamics/b2Island.cpp
    external/Box2D/Dynamics/b2Island.h
    external/Box2D/Dynamics/b2TimeStep.h
    external/Box2D/Dynamics/b2WorldCallbacks.cpp
    external/Box2D/Dynamics/b2WorldCallbacks.h
    external/Box2D/Dynamics/b2World.cpp
    external/Box2D/Dynamics/b2World.h
    external/Box2D/Dynamics/Contacts/b2CircleContact.cpp
    external/Box2D/Dynamics/Contacts/b2CircleContact.h
    external/Box2D/Dynamics/Contacts/b2Contact.cpp
    external/Box2D/Dynamics/Contacts/b2Contact.h
    external/Box2D/Dynamics/Contacts/b2ContactSolver.cpp
    external/Box2D/Dynamics/Contacts/b2ContactSolver.h
    external/Box2D/Dynamics/Contacts/b2EdgeAndCircleContact.cpp
    external/Box2D/Dynamics/Contacts/b2EdgeAndCircleContact.h
    external/Box2D/Dynamics/Contacts/b2EdgeAndPolygonContact.cpp
    external/Box2D/Dynamics/Contacts/b2EdgeAndPolygonContact.h
    external/Box2D/Dynamics/Contacts/b2LoopAndCircleContact.cpp
    external/Box2D/Dynamics/Contacts/b2LoopAndCircleContact.h
    external/Box2D/Dynamics/Contacts/b2LoopAndPolygonContact.cpp
    external/Box2D/Dynamics/Contacts/b2LoopAndPolygonContact.h
    external/Box2D/Dynamics/Contacts/b2PolygonAndCircleContact.cpp
    external/Box2D/Dynamics/Contacts/b2PolygonAndCircleContact.h
    external/Box2D/Dynamics/Contacts/b2PolygonContact.cpp
    external/Box2D/Dynamics/Contacts/b2PolygonContact.h
    external/Box2D/Dynamics/Joints/b2DistanceJoint.cpp
    external/Box2D/Dynamics/Joints/b2DistanceJoint.h
    external/Box2D/Dynamics/Joints/b2FrictionJoint.cpp
    external/Box2D/Dynamics/Joints/b2FrictionJoint.h
    external/Box2D/Dynamics/Joints/b2GearJoint.cpp
    external/Box2D/Dynamics/Joints/b2GearJoint.h
    external/Box2D/Dynamics/Joints/b2Joint.cpp
    external/Box2D/Dynamics/Joints/b2Joint.h
    external/Box2D/Dynamics/Joints/b2LineJoint.cpp
    external/Box2D/Dynamics/Joints/b2LineJoint.h
    external/Box2D/Dynamics/Joints/b2MouseJoint.cpp
    external/Box2D/Dynamics/Joints/b2MouseJoint.h
    external/Box2D/Dynamics/Joints/b2PrismaticJoint.cpp
    external/Box2D/Dynamics/Joints/b2PrismaticJoint.h
    external/Box2D/Dynamics/Joints/b2PulleyJoint.cpp
    external/Box2D/Dynamics/Joints/b2PulleyJoint.h
    external/Box2D/Dynamics/Joints/b2RevoluteJoint.cpp
    external/Box2D/Dynamics/Joints/b2RevoluteJoint.h
    external/Box2D/Dynamics/Joints/b2RopeJoint.cpp
    external/Box2D/Dynamics/Joints/b2RopeJoint.h
    external/Box2D/Dynamics/Joints/b2WeldJoint.cpp
    external/Box2D/Dynamics/Joints/b2WeldJoint.h

)

target_sources(kolfprivate PRIVATE
    ball.cpp
    ball.h
    canvasitem.cpp
    canvasitem.h
    config.cpp
    config.h
    dataserver.cpp
    dataServer.h
    editor.cpp
    editor.h
    game.cpp
    game.h
    itemfactory.cpp
    itemfactory.h
    kcomboboxdialog.cpp
    kcomboboxdialog.h
    kolf.cpp
    kolf.h
    landscape.cpp
    landscape.h
    newgame.cpp
    newgame.h
    objects.cpp
    objects.h
    obstacles.cpp
    obstacles.h
    overlay.cpp
    overlay.h
    scoreboard.cpp
    scoreboard.h
    shape.cpp
    shape.h
    tagaro/board.cpp
    tagaro/board.h
    tagaro/scene.cpp
    tagaro/scene.h
    tagaro/scene_p.h
    tagaro/spriteobjectitem.cpp
    tagaro/spriteobjectitem.h
    utils-animateditem.cpp
    utils-animateditem.h
    vector.h

)

if (QT_MAJOR_VERSION STREQUAL "6")
    target_link_libraries(kolfprivate KDEGames6)
else()
    target_link_libraries(kolfprivate KF5KDEGames)
endif()

target_link_libraries(kolfprivate
    KF${KF_MAJOR_VERSION}::Crash
    KF${KF_MAJOR_VERSION}::Completion
    KF${KF_MAJOR_VERSION}::ConfigCore
    KF${KF_MAJOR_VERSION}::DBusAddons
    KF${KF_MAJOR_VERSION}::I18n
    KF${KF_MAJOR_VERSION}::JobWidgets
    KF${KF_MAJOR_VERSION}::KIOCore
    KF${KF_MAJOR_VERSION}::TextWidgets
    KF${KF_MAJOR_VERSION}::XmlGui
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Widgets
)

########### next target ###############

add_executable(kolf)
target_sources(kolf PRIVATE main.cpp)

file(GLOB ICONS_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/pics/*-apps-kolf.png")
ecm_add_app_icon(kolf ICONS ${ICONS_SRCS})

target_link_libraries( kolf kolfprivate)
install(TARGETS kolf  ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

if(WIN32)
    target_link_libraries(kolf wsock32 ws2_32)
else()
    find_package(Threads)
    target_link_libraries(kolf ${CMAKE_THREAD_LIBS_INIT})
endif()


########### install files ###############

install(PROGRAMS org.kde.kolf.desktop  DESTINATION  ${KDE_INSTALL_APPDIR})
install(FILES kolfui.rc DESTINATION ${KDE_INSTALL_KXMLGUIDIR}/kolf)
install(FILES intro tutorial.kolf tutorial.kolfgame  DESTINATION  ${KDE_INSTALL_DATADIR}/kolf)
#install(FILES ball.h game.h statedb.h config.h canvasitem.h object.h rtti.h vector.h floater.h slope.h  DESTINATION  ${KDE_INSTALL_INCLUDEDIR}/kolf COMPONENT Devel)
install(FILES org.kde.kolf.appdata.xml DESTINATION ${KDE_INSTALL_METAINFODIR})

ki18n_install(po)
kdoctools_install(po)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
