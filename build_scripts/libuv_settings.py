#
#
# ***********************************************************************************
# * Copyright (C) 2020 - 2021, BlockSettle AB
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

from component_configurator import Configurator

class LibUVSettings(Configurator):
    def __init__(self, settings):
        Configurator.__init__(self, settings)
        self._version = '1.41.0'
        self._script_revision = '2'
        self._package_name = 'libuv-' + self._version
        self._package_url = 'https://github.com/libuv/libuv/archive/v' + self._version + '.tar.gz'

    def get_package_name(self):
        return self._package_name

    def get_revision_string(self):
        return self._version + '-' + self._script_revision

    def get_url(self):
        return self._package_url

    def get_source_dir(self):
        return os.path.join(self._project_settings.get_sources_dir(), self._package_name)

    def get_install_dir(self):
        return os.path.join(self._project_settings.get_common_build_dir(), 'libuv')

    def is_archive(self):
        return True

    def config(self):
        command = ['cmake',
            self.get_source_dir(),
            '-DBUILD_TESTING=OFF',
        ]

        if self._project_settings.get_build_mode() == 'debug':
            command.append('-DCMAKE_BUILD_TYPE=Debug')
            if self._project_settings._is_windows:
                command.append('-DCMAKE_CONFIGURATION_TYPES=Debug')
        else:
            command.append('-DCMAKE_BUILD_TYPE=Release')
            if self._project_settings._is_windows:
                command.append('-DCMAKE_CONFIGURATION_TYPES=Release')

        # for static lib
        if self._project_settings.on_windows() and self._project_settings.get_link_mode() != 'shared':
            if self._project_settings.get_build_mode() == 'debug':
                command.append('"-DCMAKE_C_FLAGS_DEBUG=/D_DEBUG /MTd /Zi /Ob0 /Od /RTC1"')
                command.append('"-DCMAKE_CXX_FLAGS_DEBUG=/D_DEBUG /MTd /Zi /Ob0 /Od /RTC1"')
            else:
                command.append('"-DCMAKE_C_FLAGS_RELEASE=/MT /O2 /Ob2 /D NDEBUG"')
                command.append('"-DCMAKE_CXX_FLAGS_RELEASE=/MT /O2 /Ob2 /D NDEBUG"')
                command.append('"-DCMAKE_C_FLAGS_RELWITHDEBINFO=/MT /O2 /Ob2 /D NDEBUG"')
                command.append('"-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=/MT /O2 /Ob2 /D NDEBUG"')

        # if self._project_settings.on_linux():
        #     command.append('-DLWS_WITH_STATIC=ON')
        #     command.append('-DLWS_WITH_SHARED=ON')

        # if self._project_settings.on_windows():
        #     if self._project_settings.get_link_mode() == 'shared':
        #         command.append('-DLWS_WITH_STATIC=OFF')
        #         command.append('-DLWS_WITH_SHARED=ON')
        #     else:
        #         command.append('-DLWS_WITH_SHARED=OFF')
        #         command.append('-DLWS_WITH_STATIC=ON')

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
        command = ['cmake', '--build']
        if self._project_settings.get_build_mode() == 'debug':
            command.append('-DCMAKE_BUILD_TYPE=Debug')
            if self._project_settings._is_windows:
                command.append('-DCMAKE_CONFIGURATION_TYPES=Debug')
        else:
            command.append('-DCMAKE_BUILD_TYPE=Release')
            if self._project_settings._is_windows:
                command.append('-DCMAKE_CONFIGURATION_TYPES=Release')

        command.append('.')

        result = subprocess.call(command)
        return result == 0

    def install(self):
        command = ['cmake', '--build', '.', '--target', 'install']
        result = subprocess.call(command)
        return result == 0
