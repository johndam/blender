# SPDX-FileCopyrightText: 2017-2022 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

# ############################################################
# Importing - Same For All Render Layer Tests
# ############################################################

import unittest

from view_layer_common import (
    MoveSceneCollectionTesting,
    setup_extra_arguments,
)


# ############################################################
# Testing
# ############################################################

class UnitTesting(MoveSceneCollectionTesting):
    def get_reference_scene_tree_map(self):
        # original tree, no changes
        reference_tree_map = [
            ['A', [
                ['i', None],
                ['ii', None],
                ['iii', None],
            ]],
            ['B', None],
            ['C', [
                ['1', None],
                ['2', None],
                ['3', [
                    ['dog', None],
                    ['cat', None],
                ]],
            ]],
        ]
        return reference_tree_map

    def test_scene_collection_into(self):
        """
        Test outliner operations
        """
        tree = self.setup_tree()
        self.assertTrue(tree['cat'].move_into(tree['2']))
        self.assertTrue(tree['cat'].move_into(tree['3']))
        self.compare_tree_maps()


# ############################################################
# Main - Same For All Render Layer Tests
# ############################################################

if __name__ == '__main__':
    UnitTesting._extra_arguments = setup_extra_arguments(__file__)
    unittest.main()
