#
#
# ***********************************************************************************
# * Copyright (C) 2018 - 2021, BlockSettle AB
# * Distributed under the GNU Affero General Public License (AGPL v3)
# * See LICENSE or http://www.gnu.org/licenses/agpl.html
# *
# **********************************************************************************
#
#
import multiprocessing
import os
import shutil
import subprocess

from component_configurator import Configurator


class CurlSettings(Configurator):
    def __init__(self, settings):
        Configurator.__init__(self, settings)
        self._version = '7_80_0'
        self._script_revision = '4'
        self._package_name = 'curl-' + self._version
        self._package_url = 'https://github.com/curl/curl/archive/' + self._package_name + '.tar.gz'
        self._package_dir_name = 'curl-' + self._package_name

    def get_package_name(self):
        return self._package_name

    def get_revision_string(self):
        return self._version + '-' + self._script_revision

    def get_url(self):
        return self._package_url

    def get_install_dir(self):
        return os.path.join(self._project_settings.get_common_build_dir(), 'curl')

    def is_archive(self):
        return True

    def config(self):
        self.copy_sources_to_build()

        print('Generating curl solution')

        command = ['cmake',
            '-DCURL_DISABLE_FTP=ON',
            '-DCURL_DISABLE_LDAP=ON',
            '-DCURL_DISABLE_LDAPS=ON',
            '-DCURL_DISABLE_TELNET=ON',
            '-DCURL_DISABLE_DICT=ON',
            '-DCURL_DISABLE_FILE=ON',
            '-DCURL_DISABLE_TFTP=ON',
            '-DCURL_DISABLE_RTSP=ON',
            '-DCURL_DISABLE_POP3=ON',
            '-DCURL_DISABLE_IMAP=ON',
            '-DCURL_DISABLE_GOPHER=ON',
            '-DCMAKE_USE_OPENSSL=ON',
            '-DOPENSSL_ROOT_DIR=' + os.path.join(self._project_settings.get_common_build_dir(), 'OpenSSL'),
            '-DCMAKE_INSTALL_PREFIX=' + self.get_install_dir(),
            '-DBUILD_SHARED_LIBS=OFF',
            '-DBUILD_CURL_EXE=OFF',
            '-DBUILD_TESTING=OFF',
            '-G', self._project_settings.get_cmake_generator()
        ]
            
        if self._project_settings.on_windows():
            command.append('-A x64 ')
            cmdStr = r' '.join(command)
            result = subprocess.call(cmdStr)
        else:
            result = subprocess.call(command)
        return result == 0

    def get_solution_file(self):
        return os.path.join(self.get_build_dir(), 'CURL.sln')

    def make(self):
        command = ['cmake', '--build', '.']
        result = subprocess.call(command)
        return result == 0

    def install(self):
        command = ['cmake', '--build', '.', '--target', 'install']
        result = subprocess.call(command)
        return result == 0
