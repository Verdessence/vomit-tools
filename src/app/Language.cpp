//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Language.h.
//
//===========================================================================//
#include "Language.h"

#include <QFile>

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string ReadResource( const QString &szPath )
{
	QFile file{ szPath };
	return file.open( QIODevice::ReadOnly ) ? file.readAll().toStdString() : std::string{};
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const xv::particles::CNames &ParticleNames()
{
	static const xv::particles::CNames names = xv::particles::CNames::Parse( ReadResource( QStringLiteral( ":/data/lang_en.txt" ) ), ReadResource( QStringLiteral( ":/data/particle_inputs.txt" ) ) );
	return names;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const xv::material::CParameterTable &MaterialParameters()
{
	static const xv::material::CParameterTable table = xv::material::CParameterTable::Parse( ReadResource( QStringLiteral( ":/data/material_parameters.txt" ) ) );
	return table;
}
