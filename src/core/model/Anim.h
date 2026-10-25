//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Checked compressed studio animation sampling and weighted skeletal deformation.
//===========================================================================//
#pragma once
#include "Mdl.h"
#include <span>
namespace xv::model
{
// Bone-to-model transforms, mapped by name when the animation comes from an included model.
[[nodiscard]] std::optional<std::vector<CTransform>> SampleSequence( const CStudioFile &source, std::size_t nSequence, float flCycle, std::span<const float> parameters, const CModel &target, std::string *pError = nullptr );
// Keeps the input immutable: both the mesh and its overlay skeleton move together.
[[nodiscard]] CModel Deform( const CModel &reference, std::span<const CTransform> pose );
} // namespace xv::model
