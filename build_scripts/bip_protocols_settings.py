import multiprocessing
import os
import shutil
import subprocess

from component_configurator import Configurator


class BipProtocolsSettings(Configurator):
    def __init__(self, settings):
        Configurator.__init__(self, settings)
        ref_version = subprocess.check_output("git ls-remote https://github.com/bitcoin/bips.git master", shell=True)
        self._version = ref_version.decode().split('\t')[0]
        self._script_revision = '1'
        self._package_name = 'bips-' + self._version
        self._package_url = 'https://github.com/bitcoin/bips/archive/master.zip'
        self._package_dir_name = self._package_name

    def get_package_name(self):
        return self._package_name

    def get_revision_string(self):
        return self._version + '-' + self._script_revision

    def get_url(self):
        return self._package_url

    def get_install_dir(self):
        return os.path.join(self._project_settings.get_common_build_dir(), 'bips')

    def is_archive(self):
        return True

    def config(self):
        return True

    def make(self):
        return True

    def install(self):
        os.rename(os.path.join(self._project_settings.get_sources_dir(), "bips-master"),  self.get_unpacked_sources_dir())
        self.filter_copy(self.get_unpacked_sources_dir(), self.get_install_dir())

        return True


