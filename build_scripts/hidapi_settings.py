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


class HidapiSettings(Configurator):
    def __init__(self, settings):
        Configurator.__init__(self, settings)
        self._version = '0.9.0'
        self._script_revision = '3'
        self._package_name = 'hidapi-' + self._version
        self._package_url = 'https://github.com/libusb/hidapi/archive/' + self._package_name + '.tar.gz'
        self._package_dir_name = 'hidapi-' + self._package_name

    def get_package_name(self):
        return self._package_name

    def get_revision_string(self):
        return self._version + '-' + self._script_revision

    def get_url(self):
        return self._package_url

    def get_install_dir(self):
        return os.path.join(self._project_settings.get_common_build_dir(), 'hidapi')

    def is_archive(self):
        return True

    def config(self):
        return True

    def make(self):
        return True

    def install(self):
        self.filter_copy(os.path.join(self.get_unpacked_sources_dir(), 'hidapi'), self.get_install_dir(), '.h')

        sourceDir = ""
        if self._project_settings.on_windows():
            sourceDir = 'windows'
        elif self._project_settings.on_linux():
            sourceDir = "libusb"
        elif self._project_settings.on_osx():
            sourceDir = "mac"

        self.filter_copy(os.path.join(self.get_unpacked_sources_dir(), sourceDir), self.get_install_dir(), '.c', False)

        return True


