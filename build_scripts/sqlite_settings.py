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
import os
import subprocess
import shutil

from component_configurator import Configurator


class SqliteSettings(Configurator):
    def __init__(self, settings):
        Configurator.__init__(self, settings)
        self._version = '3360000'
        self._script_revision = '1'
        self._package_name = "sqlite-amalgamation-" + self._version
        self._package_url = "https://www.sqlite.org/2021/" + self._package_name + ".zip"

    def get_package_name(self):
        return self._package_name

    def get_revision_string(self):
        return self._version

    def get_url(self):
        return self._package_url

    def get_install_dir(self):
        return os.path.join(self._project_settings.get_common_build_dir(), 'SQLite3')

    def is_archive(self):
        return True

    def config(self):
        return True

    def make(self):
        return True

    def install(self):
        self.filter_copy(self.get_unpacked_sources_dir(), self.get_install_dir())
        cwd = os.getcwd()
        os.chdir(self.get_install_dir())

        env_vars = os.environ.copy()

        if self._project_settings.on_windows():
           subprocess.call(["cl", "sqlite3.c", "/c"], env=env_vars)
           subprocess.call(["lib", "sqlite3.obj"], env=env_vars)
        else:
           subprocess.call(["cc", "-c", "-o", "sqlite3.o", "sqlite3.c"], env=env_vars)
           subprocess.call(["ar", "rcs", "libsqlite3.a", "sqlite3.o"], env=env_vars)

        os.chdir(cwd)
        return True
