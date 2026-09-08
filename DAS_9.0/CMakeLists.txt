cmake_minimum_required(VERSION 3.16)

project(Real_Das VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含头文件目录
include_directories(${CMAKE_SOURCE_DIR}/fftw-3.3.5-dll64)
add_subdirectory(iir1-master)

# 查找Qt6组件
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core Gui Widgets Charts Multimedia WebSockets Concurrent
)
find_package(OpenGL REQUIRED)

# Qt标准项目设置
qt_standard_project_setup(REQUIRES 6.5)

# Windows图标配置
set(APP_ICON_RESOURCE "")
if (WIN32)
    # 确保rc文件存在，并且ico文件路径正确
    set(APP_ICON_RESOURCE ${CMAKE_SOURCE_DIR}/app_icon.rc)
    # 告诉CMake处理RC文件
    enable_language(RC)
endif()

# 收集源文件
set(SOURCES
    main.cpp
    audio.h audio.cpp
    receive_data.h receive_data.cpp
    Filter.h Filter.cpp
    rms_calculate.h rms_calculate.cpp
    my.h
    my.cu
    qcustomplot.h qcustomplot.cpp
    mainwindow.h mainwindow.cpp mainwindow.ui
    fftw3.h
    fftw_guard.h fftw_guard.cpp
    fft_calculate.h fft_calculate.cpp
    rebuild_data.h rebuild_data.cpp
    unwrap.h unwrap.cpp
    parametermanager.h parametermanager.cpp
    sava_data.h sava_data.cpp
    mainwindowutils.h mainwindowutils.cpp
    mainwindowplot.h mainwindowplot.cpp
    mainwindowdata.h mainwindowdata.cpp
    saveddataviewer.h saveddataviewer.cpp
)

# 添加可执行文件
qt_add_executable(Real_Das
    ${SOURCES}
    app_resources.qrc  # Qt资源文件（窗口图标）
    ${APP_ICON_RESOURCE}  # Windows EXE图标
)

# 显式加入工程根目录，保证 qcustomplot.h、mainwindow*.h 等本地头文件
# 始终从当前工程路径查找，不受旧的 Qt Creator/CMake 构建缓存影响。
target_include_directories(Real_Das PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fftw-3.3.5-dll64"
)

# QCustomPlot 的单个源文件较大。MinGW 默认 COFF 目标文件段数有限，
# 编译时会出现“too many sections / file too big”；为 MinGW 开启大目标文件支持。
if (MINGW)
    target_compile_options(Real_Das PRIVATE -Wa,-mbig-obj)
endif()

# 设置目标属性
set_target_properties(Real_Das PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE  # Windows下编译为GUI程序（无控制台）
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON  # 关键：启用Qt资源文件自动处理
)

# 构建类型
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 链接库
target_link_libraries(Real_Das
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets  # 补充Widgets组件（MainWindow依赖）
    Qt6::Charts
    Qt6::Multimedia
    Qt6::WebSockets
    Qt6::Concurrent
    OpenGL::GL
    setupapi
    iir_static
)

# FFTW链接
if (WIN32)
    target_link_libraries(Real_Das PRIVATE ${CMAKE_SOURCE_DIR}/fftw-3.3.5-dll64/libfftw3f-3.lib)
elseif (UNIX)
    target_link_libraries(Real_Das PRIVATE ${CMAKE_SOURCE_DIR}/fftw-3.3.5-dll64/libfftw3f-3.a)
endif()

# 编译定义
target_compile_definitions(Real_Das PRIVATE
    QCUSTOMPLOT_USE_OPENGL
    QT_DEPRECATED_WARNINGS
)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")

# 安装规则
include(GNUInstallDirs)
install(TARGETS Real_Das
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
