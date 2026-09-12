# project_drl/test/utils.py

import os
import sys

def add_project_root():
    """
    将 project_drl 目录添加到 sys.path，保证 lib/src/tool 可导入
    """
    current_file = os.path.abspath(__file__)
    project_root = os.path.abspath(os.path.join(current_file, '..', '..'))
    if project_root not in sys.path:
        sys.path.insert(0, project_root)
