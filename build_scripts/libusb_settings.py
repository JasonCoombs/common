#
#
# ***********************************************************************************
# * Copyright (C) 2018 - 2020, BlockSettle AB
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


class LibusbSettings(Configurator):
    def __init__(self, settings):
        Configurator.__init__(self, settings)
        self._version = '1.0.23'
        self._script_revision = '3'
        self._package_name = 'libusb-' + self._version
        self._package_dir_name = self._package_name
        self._package_url = 'https://github.com/libusb/libusb/releases/download/v' + self._version + '/' + self._package_dir_name + '.tar.bz2'

    def get_package_name(self):
        return self._package_name

    def get_revision_string(self):
        return self._version + '-' + self._script_revision

    def get_url(self):
        return self._package_url

    def get_install_dir(self):
        return os.path.join(self._project_settings.get_common_build_dir(), 'libusb')

    def is_archive(self):
        return True

    def config(self):
        if self._project_settings.on_windows():
            return True

        command = [os.path.join(self.get_unpacked_sources_dir(), "configure")]
        result = subprocess.call(command)
        return result == 0

    def make(self):
        if self._project_settings.on_windows():
            return True

        command = ["make"]
        result = subprocess.call(command)
        return result == 0


    def install(self):
        if not os.path.isdir(self.get_install_dir()):
            os.makedirs(self.get_install_dir())

        if self._project_settings.on_windows():
            return True

        src_lib_dir = os.path.join(self.get_build_dir(), 'libusb', '.libs')
        install_lib_dir = os.path.join(self.get_install_dir(), 'lib')

        self.filter_copy(src_lib_dir, install_lib_dir)

        src_include_dir = os.path.join(self.get_unpacked_sources_dir(), 'libusb')
        install_include_dir = os.path.join(self.get_install_dir(), 'include')
        self.filter_copy(src_include_dir, install_include_dir)

        return True

