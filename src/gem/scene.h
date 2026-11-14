// Composes a full Atari screen for one resource item: desktop, menu bar,
// dialogs centred like form_center(), alerts like form_alert().
#pragma once
#include "gem/aes.h"

namespace gem {

enum class ItemKind { Tree, String, Image };

struct SceneOptions {
    bool desktop = true;
    int openMenu = -1;          // index of menu title whose dropdown is shown
    int alertDefault = 1;       // default button for alerts
};

struct Scene {
    rsc::Tree tree;             // the tree actually drawn (alerts are built)
    Layout L;
    bool isMenu = false;
    int openBox = -1;           // dropdown box drawn (menus)
};

// Render resource item (kind, index) of r into s.  Returns the scene so
// callers can map mouse positions to objects.
Scene renderItem(Aes& aes, const rsc::Rsc& r, ItemKind kind, int index, const SceneOptions& opt);
// Same as above for a tree that is not (yet) part of an Rsc.
Scene renderTree(Aes& aes, const rsc::Tree& t, const SceneOptions& opt);

} // namespace gem
