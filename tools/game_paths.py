"""Where the games are installed, for the tools in this directory.

Set DRAKENSANG_DIR (Drakensang, 2008) and DRAKENSANG_ROT_DIR (The River of Time). If tools/ sits
inside <game>/_mod, that game is found without DRAKENSANG_DIR. A game that is not set stays '' and
the tools skip it or stop with need_game().
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def _enclosing_game():
    root = os.path.dirname(os.path.dirname(HERE))
    return root if os.path.basename(os.path.dirname(HERE)) == '_mod' else ''


D1_DIR = os.environ.get('DRAKENSANG_DIR') or _enclosing_game()
ROT_DIR = os.environ.get('DRAKENSANG_ROT_DIR', '')


def need_game(path, var='DRAKENSANG_DIR'):
    """Return path if it is an existing directory, otherwise stop with a message."""
    if not path or not os.path.isdir(path):
        sys.exit('game directory not found (%r): set %s to the game folder' % (path, var))
    return path


def dump_file(gamedir, name):
    """Decrypted image the mod wrote with [dev] dump_image=1; '' when the game is not set."""
    return os.path.join(gamedir, '_mod', 'dump', name) if gamedir else ''
