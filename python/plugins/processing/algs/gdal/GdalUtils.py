# -*- coding: utf-8 -*-

"""
***************************************************************************
    GdalUtils.py
    ---------------------
    Date                 : August 2012
    Copyright            : (C) 2012 by Victor Olaya
    Email                : volayaf at gmail dot com
***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************
"""
from builtins import str
from builtins import range
from builtins import object

__author__ = 'Victor Olaya'
__date__ = 'August 2012'
__copyright__ = '(C) 2012, Victor Olaya'

# This will get replaced with a git SHA1 when you do a git archive

__revision__ = '$Format:%H$'

import os
import subprocess
import platform

from osgeo import gdal

from qgis.core import (QgsApplication,
                       QgsVectorFileWriter,
                       QgsProcessingFeedback,
                       QgsSettings)
from processing.core.ProcessingConfig import ProcessingConfig
from processing.core.ProcessingLog import ProcessingLog
from processing.tools.system import isWindows, isMac

try:
    from osgeo import gdal  # NOQA
    gdalAvailable = True
except:
    gdalAvailable = False


class GdalUtils(object):

    GDAL_HELP_PATH = 'GDAL_HELP_PATH'

    supportedRasters = None

    @staticmethod
    def runGdal(commands, feedback=None):
        if feedback is None:
            feedback = QgsProcessingFeedback()
        envval = os.getenv('PATH')
        # We need to give some extra hints to get things picked up on OS X
        isDarwin = False
        try:
            isDarwin = platform.system() == 'Darwin'
        except IOError:  # https://travis-ci.org/m-kuhn/QGIS#L1493-L1526
            pass
        if isDarwin and os.path.isfile(os.path.join(QgsApplication.prefixPath(), "bin", "gdalinfo")):
            # Looks like there's a bundled gdal. Let's use it.
            os.environ['PATH'] = "{}{}{}".format(os.path.join(QgsApplication.prefixPath(), "bin"), os.pathsep, envval)
            os.environ['DYLD_LIBRARY_PATH'] = os.path.join(QgsApplication.prefixPath(), "lib")
        else:
            # Other platforms should use default gdal finder codepath
            settings = QgsSettings()
            path = settings.value('/GdalTools/gdalPath', '')
            if not path.lower() in envval.lower().split(os.pathsep):
                envval += '{}{}'.format(os.pathsep, path)
                os.putenv('PATH', envval)

        fused_command = ' '.join([str(c) for c in commands])
        ProcessingLog.addToLog(ProcessingLog.LOG_INFO, fused_command)
        feedback.pushInfo('GDAL command:')
        feedback.pushCommandInfo(fused_command)
        feedback.pushInfo('GDAL command output:')
        success = False
        retry_count = 0
        while not success:
            loglines = []
            loglines.append('GDAL execution console output')
            try:
                with subprocess.Popen(
                    fused_command,
                    shell=True,
                    stdout=subprocess.PIPE,
                    stdin=subprocess.DEVNULL,
                    stderr=subprocess.STDOUT,
                    universal_newlines=True,
                ) as proc:
                    for line in proc.stdout:
                        feedback.pushConsoleInfo(line)
                        loglines.append(line)
                    success = True
            except IOError as e:
                if retry_count < 5:
                    retry_count += 1
                else:
                    raise IOError(e.message + u'\nTried 5 times without success. Last iteration stopped after reading {} line(s).\nLast line(s):\n{}'.format(len(loglines), u'\n'.join(loglines[-10:])))

        ProcessingLog.addToLog(ProcessingLog.LOG_INFO, loglines)
        GdalUtils.consoleOutput = loglines

    @staticmethod
    def getConsoleOutput():
        return GdalUtils.consoleOutput

    @staticmethod
    def getSupportedRasters():
        if not gdalAvailable:
            return {}

        if GdalUtils.supportedRasters is not None:
            return GdalUtils.supportedRasters

        if gdal.GetDriverCount() == 0:
            gdal.AllRegister()

        GdalUtils.supportedRasters = {}
        GdalUtils.supportedRasters['GTiff'] = ['tif']
        for i in range(gdal.GetDriverCount()):
            driver = gdal.GetDriver(i)
            if driver is None:
                continue
            shortName = driver.ShortName
            metadata = driver.GetMetadata()
            # ===================================================================
            # if gdal.DCAP_CREATE not in metadata \
            #         or metadata[gdal.DCAP_CREATE] != 'YES':
            #     continue
            # ===================================================================
            if gdal.DMD_EXTENSION in metadata:
                extensions = metadata[gdal.DMD_EXTENSION].split('/')
                if extensions:
                    GdalUtils.supportedRasters[shortName] = extensions

        return GdalUtils.supportedRasters

    @staticmethod
    def getSupportedRasterExtensions():
        allexts = ['tif']
        for exts in list(GdalUtils.getSupportedRasters().values()):
            for ext in exts:
                if ext not in allexts and ext != '':
                    allexts.append(ext)
        return allexts

    @staticmethod
    def getVectorDriverFromFileName(filename):
        ext = os.path.splitext(filename)[1]
        if ext == '':
            return 'ESRI Shapefile'

        formats = QgsVectorFileWriter.supportedFiltersAndFormats()
        for k, v in list(formats.items()):
            if ext in k:
                return v
        return 'ESRI Shapefile'

    @staticmethod
    def getFormatShortNameFromFilename(filename):
        ext = filename[filename.rfind('.') + 1:]
        supported = GdalUtils.getSupportedRasters()
        for name in list(supported.keys()):
            exts = supported[name]
            if ext in exts:
                return name
        return 'GTiff'

    @staticmethod
    def escapeAndJoin(strList):
        joined = ''
        for s in strList:
            if s[0] != '-' and ' ' in s:
                escaped = '"' + s.replace('\\', '\\\\').replace('"', '\\"') \
                    + '"'
            else:
                escaped = s
            joined += escaped + ' '
        return joined.strip()

    @staticmethod
    def version():
        return int(gdal.VersionInfo('VERSION_NUM'))

    @staticmethod
    def readableVersion():
        return gdal.VersionInfo('RELEASE_NAME')

    @staticmethod
    def gdalHelpPath():
        helpPath = ProcessingConfig.getSetting(GdalUtils.GDAL_HELP_PATH)

        if helpPath is None:
            if isWindows():
                pass
            elif isMac():
                pass
            else:
                searchPaths = ['/usr/share/doc/libgdal-doc/gdal']
                for path in searchPaths:
                    if os.path.exists(path):
                        helpPath = os.path.abspath(path)
                        break

        return helpPath if helpPath is not None else 'http://www.gdal.org/'
