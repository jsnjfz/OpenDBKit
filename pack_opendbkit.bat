@echo off
REM OpenDBKit build + deploy script

set SCRIPT_DIR=%~dp0
set BUILD_DIR=%SCRIPT_DIR%build
set DIST_DIR=%SCRIPT_DIR%dist

if "%QTDIR%"=="" (
    set QTDIR=C:\Qt\5.15.2\mingw81_64
)
if "%MINGW_DIR%"=="" (
    set MINGW_DIR=C:\Qt\Tools\mingw810_64
)
set PATH=%QTDIR%\bin;%MINGW_DIR%\bin;%PATH%

if not exist "%QTDIR%\bin\qmake.exe" (
    echo [ERROR] Cannot find qmake under %QTDIR%\bin. Please set QTDIR properly.
    goto :eof
)

if exist "%BUILD_DIR%" (
    echo [INFO] Cleaning %BUILD_DIR%
    rmdir /s /q "%BUILD_DIR%"
)
mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

echo [INFO] Running qmake...
"%QTDIR%\bin\qmake.exe" "%SCRIPT_DIR%cpp\OpenDBKit.pro" -config release
if errorlevel 1 goto :error

echo [INFO] Building with mingw32-make...
mingw32-make -j4
if errorlevel 1 goto :error

set RELEASE_EXE=%BUILD_DIR%\release\opendbkit.exe
if not exist "%RELEASE_EXE%" (
    set RELEASE_EXE=%BUILD_DIR%\opendbkit.exe
)
if not exist "%RELEASE_EXE%" (
    echo [ERROR] Build output not found: %RELEASE_EXE%
    goto :error
)

if exist "%DIST_DIR%" (
    echo [INFO] Cleaning %DIST_DIR%
    rmdir /s /q "%DIST_DIR%"
)
mkdir "%DIST_DIR%"

if exist "%QTDIR%\bin\windeployqt.exe" (
    echo [INFO] Deploying Qt runtime...
    "%QTDIR%\bin\windeployqt.exe" --release "%RELEASE_EXE%"
) else (
    echo [WARN] windeployqt not found, copying plugins manually if available.
)

echo [INFO] Copying artifacts to %DIST_DIR%...
xcopy /y /q "%RELEASE_EXE%" "%DIST_DIR%\" >nul
if exist "%BUILD_DIR%\*.dll" (
    xcopy /y /q "%BUILD_DIR%\*.dll" "%DIST_DIR%\" >nul
)
for %%L in (Qt5Core.dll Qt5Gui.dll Qt5Widgets.dll Qt5Sql.dll Qt5Svg.dll Qt5Network.dll Qt5PrintSupport.dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
    if not exist "%DIST_DIR%\%%L" (
        if exist "%QTDIR%\bin\%%L" (
            copy /y "%QTDIR%\bin\%%L" "%DIST_DIR%\%%L" >nul
        )
    )
)
if exist "%BUILD_DIR%\release\platforms" (
    xcopy /y /e /q "%BUILD_DIR%\release\platforms" "%DIST_DIR%\platforms\" >nul
) else if exist "%BUILD_DIR%\platforms" (
    xcopy /y /e /q "%BUILD_DIR%\platforms" "%DIST_DIR%\platforms\" >nul
) else if exist "%QTDIR%\plugins\platforms" (
    echo [INFO] Copying default Qt platforms...
    xcopy /y /e /q "%QTDIR%\plugins\platforms" "%DIST_DIR%\platforms\" >nul
)
if exist "%BUILD_DIR%\release\sqldrivers" (
    xcopy /y /e /q "%BUILD_DIR%\release\sqldrivers" "%DIST_DIR%\sqldrivers\" >nul
) else if exist "%BUILD_DIR%\sqldrivers" (
    xcopy /y /e /q "%BUILD_DIR%\sqldrivers" "%DIST_DIR%\sqldrivers\" >nul
) else if exist "%QTDIR%\plugins\sqldrivers" (
    echo [INFO] Copying Qt SQL drivers...
    xcopy /y /e /q "%QTDIR%\plugins\sqldrivers" "%DIST_DIR%\sqldrivers\" >nul
)
if exist "%BUILD_DIR%\release\iconengines" (
    xcopy /y /e /q "%BUILD_DIR%\release\iconengines" "%DIST_DIR%\iconengines\" >nul
) else if exist "%BUILD_DIR%\iconengines" (
    xcopy /y /e /q "%BUILD_DIR%\iconengines" "%DIST_DIR%\iconengines\" >nul
) else if exist "%QTDIR%\plugins\iconengines" (
    echo [INFO] Copying Qt icon engines...
    xcopy /y /e /q "%QTDIR%\plugins\iconengines" "%DIST_DIR%\iconengines\" >nul
)
if exist "%BUILD_DIR%\release\imageformats" (
    xcopy /y /e /q "%BUILD_DIR%\release\imageformats" "%DIST_DIR%\imageformats\" >nul
) else if exist "%BUILD_DIR%\imageformats" (
    xcopy /y /e /q "%BUILD_DIR%\imageformats" "%DIST_DIR%\imageformats\" >nul
) else if exist "%QTDIR%\plugins\imageformats" (
    echo [INFO] Copying Qt image formats...
    xcopy /y /e /q "%QTDIR%\plugins\imageformats" "%DIST_DIR%\imageformats\" >nul
)
if exist "%BUILD_DIR%\release\styles" (
    xcopy /y /e /q "%BUILD_DIR%\release\styles" "%DIST_DIR%\styles\" >nul
) else if exist "%BUILD_DIR%\styles" (
    xcopy /y /e /q "%BUILD_DIR%\styles" "%DIST_DIR%\styles\" >nul
) else if exist "%QTDIR%\plugins\styles" (
    echo [INFO] Copying Qt styles...
    xcopy /y /e /q "%QTDIR%\plugins\styles" "%DIST_DIR%\styles\" >nul
)

set MYSQL_RUNTIME=%SCRIPT_DIR%third_party\mysql57\libmysql.dll
if exist "%MYSQL_RUNTIME%" (
    echo [INFO] Copying libmysql runtime...
    copy /y "%MYSQL_RUNTIME%" "%DIST_DIR%\libmysql.dll" >nul
) else (
    echo [WARN] libmysql.dll not found under %SCRIPT_DIR%third_party\mysql57. MySQL connections will fail.
)

REM Copy project resources
if exist "%SCRIPT_DIR%lib" (
    echo [INFO] Copying lib directory...
    xcopy /y /e /q "%SCRIPT_DIR%lib" "%DIST_DIR%\lib\" >nul
)
if exist "%SCRIPT_DIR%language" (
    echo [INFO] Copying language directory...
    xcopy /y /e /q "%SCRIPT_DIR%language" "%DIST_DIR%\language\" >nul
)
if exist "%SCRIPT_DIR%img" (
    echo [INFO] Copying img directory...
    xcopy /y /e /q "%SCRIPT_DIR%img" "%DIST_DIR%\img\" >nul
)

echo [INFO] OpenDBKit package ready at %DIST_DIR%
echo [INFO] Run %DIST_DIR%\opendbkit.exe to test.
goto :eof

:error
echo [ERROR] Build failed, see log above.
