# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Run the bundled project wizard with isolated output and retained notices."""
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='afterlight-wizard-') as temporary:
    project = pathlib.Path(temporary) / 'New project'
    subprocess.run([sys.argv[1], '--wizard-test', str(project), '--validation'], check=True)
    # The editor appends its test-run suffix to the requested directory prefix.
    created = list(pathlib.Path(temporary).glob('New project-*'))
    assert len(created) == 1
    project = created[0]
    assert 'Mozilla Public License Version 2.0' in (project / 'LICENSE').read_text()
    assert 'SPDX-License-Identifier: MPL-2.0' in (project / 'game/game.c').read_text()
    assert 'SPDX-License-Identifier: MPL-2.0' in (project / 'include/afterlight/game_module.h').read_text()
    assert (project / 'tools/package_source.py').is_file()
    assert 'MPL-2.0' in (project / 'NOTICE').read_text()
