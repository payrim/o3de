"""
Copyright (c) Contributors to the Open 3D Engine Project. For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT

"""

# This suite consists of all test cases that are passing and have been verified.

import pytest
import os
import sys

from ly_test_tools import LAUNCHERS
sys.path.append(os.path.dirname(os.path.abspath(__file__)) + '/../automatedtesting_shared')

from base import TestAutomationBase

@pytest.mark.parametrize("launcher_platform", ['windows_editor'])
@pytest.mark.parametrize("project", ["AutomatedTesting"])
class TestAutomation(TestAutomationBase):

    use_null_renderer = False  # Use default renderer (needs gpu)
    extra_cmdline_args = []

    def test_C18977329_NvCloth_AddClothSimulationToMesh(self, request, workspace, editor, launcher_platform):
        from . import C18977329_NvCloth_AddClothSimulationToMesh as test_module
        self._run_test(request, workspace, editor, test_module, self.extra_cmdline_args, self.use_null_renderer)

    def test_C18977330_NvCloth_AddClothSimulationToActor(self, request, workspace, editor, launcher_platform):
        from . import C18977330_NvCloth_AddClothSimulationToActor as test_module
        self._run_test(request, workspace, editor, test_module, self.extra_cmdline_args, self.use_null_renderer)
