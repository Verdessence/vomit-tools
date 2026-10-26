//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Convert glTF triangle meshes to the existing model skeleton for external compilation.
//===========================================================================//
#pragma once
#include <QString>
#include "model/Model.h"
// glTF metres/right-handed Y-up become Source inches/Z-up. Unsupported encodings are refused.
[[nodiscard]] xv::model::CModel ImportGltfMesh( const QString &szFile, const xv::model::CModel &target, int nRootBone = 0 );
