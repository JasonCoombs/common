#
#
# ***********************************************************************************
# * Copyright (C) 2021, BlockSettle AB
# * Distributed under the GNU Affero General Public License (AGPL v3)
# * See LICENSE or http://www.gnu.org/licenses/agpl.html
# *
# **********************************************************************************
#
#
import os
import subprocess
import shutil
import multiprocessing
from build_scripts.openssl_settings import OpenSslSettings
from build_scripts.jansson_settings import JanssonSettings

from component_configurator import Configurator

class CJOSE(Configurator):
    def __init__(self, settings):
        Configurator.__init__(self, settings)
        self._version = '3f77bc82e078c6aa15cbffa796fb2d700612ecf4'
        self._script_revision = '1'
        self._package_name = 'cjose-master'
        self._package_url = 'https://github.com/sergey-chernikov/cjose/archive/' + self._version + '.zip'
        self.openssl = OpenSslSettings(settings)
        self.jansson = JanssonSettings(settings)

    def get_package_name(self):
        return self._package_name

    def get_revision_string(self):
        return self._version + '-' + self._script_revision

    def get_url(self):
        return self._package_url

    def get_source_dir(self):
        return os.path.join(self._project_settings.get_sources_dir(), self._package_name)

    def get_install_dir(self):
        return os.path.join(self._project_settings.get_common_build_dir(), 'cjose')

    def is_archive(self):
        return True

    def config(self):
        command = ['cmake',
            self.get_source_dir(),
            '-DOPENSSL_INCLUDE_DIR=' + os.path.join(self.openssl.get_install_dir(), 'include'),
            '-DJANSSON_INCLUDE_DIR=' + os.path.join(self.jansson.get_install_dir(), 'include'),
            '-DBUILD_INCLUDES=' + os.path.join(self.get_build_dir(), 'include')
        ]

        command.append('-G')
        command.append(self._project_settings.get_cmake_generator())
        if self._project_settings.on_windows():
            command.append('-A x64 ')

        command.append('-DCMAKE_INSTALL_PREFIX=' + self.get_install_dir())

        print(command)
        env_vars = os.environ.copy()
        if self._project_settings.on_windows():
            cmdStr = r' '.join(command)
            result = subprocess.call(cmdStr, env=env_vars)
        else:
           result = subprocess.call(command, env=env_vars)
        return result == 0

    def make(self):
        command = ['cmake', '--build', '.']
        result = subprocess.call(command)
        return result == 0

    def install(self):
        command = ['cmake', '--build', '.', '--target', 'install']
        result = subprocess.call(command)
        return result == 0
