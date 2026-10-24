//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: KeyValues blocks and keys on the clipboard, as their text, so they paste into another
//          file, another tab, or a text editor, and KeyValues text copied from anywhere pastes in.
//
//===========================================================================//
#pragma once

#include <vector>

#include "kv1/Kv1.h"

class QMimeData;

// Puts nodes on the clipboard, laid out afresh (tabs, one per line).
void CopyNodes( const std::vector<xv::kv1::CNode> &nodes );
// The nodes on the clipboard; empty when it holds no KeyValues (or text the parser stumbles on).
[[nodiscard]] std::vector<xv::kv1::CNode> PastedNodes();
// The same as data for the clipboard or a drag, and back.
[[nodiscard]] QMimeData *NodesMimeData( const std::vector<xv::kv1::CNode> &nodes );
[[nodiscard]] std::vector<xv::kv1::CNode> NodesFromMimeData( const QMimeData *pData );
// Whether the clipboard holds KeyValues to paste.
[[nodiscard]] bool CanPasteNodes();
