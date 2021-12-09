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
# this build script is still incomplete, as it requires quite a lot of other lib dependencies
# which doesn't make sense only to call the binary at generation phase

import multiprocessing
import os
import shutil
import subprocess

from component_configurator import Configurator


class gRPCSettings(Configurator):
    def __init__(self, settings):
        Configurator.__init__(self, settings)
        self._version = '1.42.0'
        self._package_name = 'grpc-' + self._version
        self._package_name_url = 'grpc-' + self._version
        self._script_revision = '3'
        self._package_url = 'https://github.com/grpc/grpc/archive/refs/tags/v' + self._version + '.zip'

    def get_package_name(self):
        return self._package_name

    def get_revision_string(self):
        return self._version + '-' + self._script_revision

    def get_url(self):
        return self._package_url

    def get_install_dir(self):
        return os.path.join(self._project_settings.get_common_build_dir(), 'grpc')

    def is_archive(self):
        return True

    def config_windows(self):
        self.copy_sources_to_build()

        command = ['cmake', '.',
                   '-G',
                   self._project_settings.get_cmake_generator(),
                   '-DgRPC_BUILD_CSHARP_EXT=OFF'
                  ]

        if self._project_settings.on_windows():
            command.append('-A x64 ')

        if self._project_settings.on_windows():
            cmdStr = r' '.join(command)
            result = subprocess.call(cmdStr)
        else:
            result = subprocess.call(command, shell=True)
        return result == 0

    def get_solution_file(self):
        return os.path.join(self.get_build_dir(), 'grpc.sln')

    def config_x(self):
        cwd = os.getcwd()
        os.chdir(self.get_unpacked_sources_dir())
        command = ['./autogen.sh']
        result = subprocess.call(command)
        if result != 0:
            return False

        os.chdir(cwd)
        command = [os.path.join(self.get_unpacked_sources_dir(), 'configure'),
                   '--prefix',
                   self.get_install_dir()]

        result = subprocess.call(command)
        return result == 0

    def make_windows(self):
        print('Making protobuf: might take a while')

        command = ['msbuild',
                   self.get_solution_file(),
                   '/t:protoc',
                   '/p:Configuration=' + self.get_win_build_mode(),
                   '/p:CL_MPCount=' + str(max(1, multiprocessing.cpu_count() - 1))]

        result = subprocess.call(command)
        return result == 0

    def make(self):
        command = ['cmake', '--build', '.']
        result = subprocess.call(command)
        return result == 0

#    def get_win_build_mode(self):
#        if self._project_settings.get_build_mode() == 'release':
#            return 'RelWithDebInfo'
#        else:
#            return 'Debug'
#
#    def make_x(self):
#        command = ['make', '-j', str(multiprocessing.cpu_count())]
#
#        result = subprocess.call(command)
#        return result == 0

    def install(self):
        command = ['cmake', '--build', '.', '--target', 'install']
        result = subprocess.call(command)
        return result == 0
