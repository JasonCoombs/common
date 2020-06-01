#
#
# ***********************************************************************************
# * Copyright (C) 2020 - 2020, BlockSettle AB
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

class AMQPCPPSettings(Configurator):
    def __init__(self, settings):
        Configurator.__init__(self, settings)
        self._version = '4.1.5'
        self._script_revision = '1'
        self._package_name = 'amqpcpp-' + self._version
        self._package_url = 'https://github.com/CopernicaMarketingSoftware/AMQP-CPP/archive/v' + self._version + '.tar.gz'

    def get_package_name(self):
        return self._package_name

    def get_revision_string(self):
        return self._version + '-' + self._script_revision

    def get_url(self):
        return self._package_url

    def get_source_dir(self):
        return os.path.join(self._project_settings.get_sources_dir(), 'AMQP-CPP-' + self._version)

    def get_install_dir(self):
        return os.path.join(self._project_settings.get_common_build_dir(), 'amqpcpp')

    def is_archive(self):
        return True

    def config(self):
        command = ['cmake',
            self.get_source_dir(),
            '-DAMQP-CPP_BUILD_SHARED=OFF',
        ]

        # for static lib
        if self._project_settings.on_windows() and self._project_settings.get_link_mode() != 'shared':
            if self._project_settings.get_build_mode() == 'debug':
                command.append('-DCMAKE_C_FLAGS_DEBUG=/D_DEBUG /MTd /Zi /Ob0 /Od /RTC1')
                command.append('-DCMAKE_CXX_FLAGS_DEBUG=/D_DEBUG /MTd /Zi /Ob0 /Od /RTC1')
            else:
                command.append('-DCMAKE_C_FLAGS_RELEASE=/MT /O2 /Ob2 /D NDEBUG')
                command.append('-DCMAKE_CXX_FLAGS_RELEASE=/MT /O2 /Ob2 /D NDEBUG')
                command.append('-DCMAKE_C_FLAGS_RELWITHDEBINFO=/MT /O2 /Ob2 /D NDEBUG')
                command.append('-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=/MT /O2 /Ob2 /D NDEBUG')

        if self._project_settings.on_linux():
            command.append('-DAMQP-CPP_LINUX_TCP=ON')

        # if self._project_settings.on_windows():
        #     if self._project_settings.get_link_mode() == 'shared':
        #         command.append('-DLWS_WITH_STATIC=OFF')
        #         command.append('-DLWS_WITH_SHARED=ON')
        #     else:
        #         command.append('-DLWS_WITH_SHARED=OFF')
        #         command.append('-DLWS_WITH_STATIC=ON')

        command.append('-G')
        command.append(self._project_settings.get_cmake_generator())

        command.append('-DCMAKE_INSTALL_PREFIX=' + self.get_install_dir())

        print(command)
        env_vars = os.environ.copy()
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
