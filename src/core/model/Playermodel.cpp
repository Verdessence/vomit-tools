//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Playermodel.h.
//
//===========================================================================//
#include "Playermodel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <stdexcept>

#include "Mdl.h"
#include "Phy.h"
#include "kv1/Kv1.h"

namespace xv::model
{

namespace
{

// What GMod's player animations move: a model without one of these can't follow them.
constexpr std::array<std::string_view, 21> PLAYER_BONES{
	"ValveBiped.Bip01_Pelvis",
	"ValveBiped.Bip01_Spine",
	"ValveBiped.Bip01_Spine1",
	"ValveBiped.Bip01_Spine2",
	"ValveBiped.Bip01_Neck1",
	"ValveBiped.Bip01_Head1",
	"ValveBiped.Bip01_L_Clavicle",
	"ValveBiped.Bip01_L_UpperArm",
	"ValveBiped.Bip01_L_Forearm",
	"ValveBiped.Bip01_L_Hand",
	"ValveBiped.Bip01_R_Clavicle",
	"ValveBiped.Bip01_R_UpperArm",
	"ValveBiped.Bip01_R_Forearm",
	"ValveBiped.Bip01_R_Hand",
	"ValveBiped.Bip01_L_Thigh",
	"ValveBiped.Bip01_L_Calf",
	"ValveBiped.Bip01_L_Foot",
	"ValveBiped.Bip01_R_Thigh",
	"ValveBiped.Bip01_R_Calf",
	"ValveBiped.Bip01_R_Foot",
	"ValveBiped.Bip01_Spine4",
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Lower( std::string_view szText )
{
	std::string sz{ szText };
	std::ranges::transform( sz, sz.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	return sz;
}

//-----------------------------------------------------------------------------
// Purpose: A Lua string literal.
//-----------------------------------------------------------------------------
std::string LuaString( std::string_view szText )
{
	std::string out = "\"";
	for ( const char ch : szText )
	{
		if ( ch == '"' || ch == '\\' )
			out += '\\';
		if ( ch == '\n' || ch == '\r' )
			continue;
		out += ch;
	}
	return out + '"';
}

//-----------------------------------------------------------------------------
// Purpose: The .phy's pieces as a mesh on the model's bones, each piece weighted wholly to its
//          bone, in the reference pose.
//-----------------------------------------------------------------------------
CModel RagdollMesh( const CModel &model, const Physics_t &physics )
{
	CModel mesh;
	mesh.m_Bones = model.m_Bones;
	mesh.m_Materials = { "phy" };
	Mesh_t part;
	part.m_nMaterial = 0;
	for ( const Convex_t &convex : physics.m_Convexes )
	{
		if ( convex.m_nBone < 0 || convex.m_nBone >= static_cast<int>( model.m_Bones.size() ) )
			continue;
		const CTransform &toModel = model.m_Bones[ static_cast<std::size_t>( convex.m_nBone ) ].m_ToModel;
		for ( std::size_t i = 0; i + 2 < convex.m_Indices.size(); i += 3 )
		{
			std::array<CVec3, 3> corners;
			bool bValid = true;
			for ( int j = 0; j < 3; j++ )
			{
				const std::uint32_t n = convex.m_Indices[ i + static_cast<std::size_t>( j ) ];
				bValid = bValid && n < convex.m_Vertices.size();
				if ( bValid )
					corners[ static_cast<std::size_t>( j ) ] = toModel.Apply( convex.m_Vertices[ n ] );
			}
			if ( !bValid )
				continue;
			const CVec3 a = corners[ 1 ] - corners[ 0 ], b = corners[ 2 ] - corners[ 0 ];
			CVec3 normal{ a.m_flY * b.m_flZ - a.m_flZ * b.m_flY, a.m_flZ * b.m_flX - a.m_flX * b.m_flZ, a.m_flX * b.m_flY - a.m_flY * b.m_flX };
			const float flLength = std::sqrt( normal.m_flX * normal.m_flX + normal.m_flY * normal.m_flY + normal.m_flZ * normal.m_flZ );
			if ( flLength > 0.0f )
				normal = normal * ( 1.0f / flLength );
			for ( const CVec3 &corner : corners )
			{
				Vertex_t vertex;
				vertex.m_vecPosition = corner;
				vertex.m_vecNormal = normal;
				vertex.m_Bones[ 0 ] = convex.m_nBone;
				vertex.m_Weights[ 0 ] = 1.0f;
				vertex.m_nBones = 1;
				part.m_Indices.push_back( static_cast<std::uint32_t>( mesh.m_Vertices.size() ) );
				mesh.m_Vertices.push_back( vertex );
			}
		}
	}
	mesh.m_Meshes.push_back( std::move( part ) );
	return mesh;
}

//-----------------------------------------------------------------------------
// Purpose: The .phy's text as $collisionjoints' contents: the total mass, the first piece's
//          damping and inertia, the root bone, and each joint's limits on its child bone.
//-----------------------------------------------------------------------------
std::string RagdollJoints( const Physics_t &physics, const CModel &model )
{
	const auto parsed = kv1::Parse( physics.m_szText );
	std::vector<std::string> names( static_cast<std::size_t>( std::max( physics.m_nSolids, 0 ) ) );
	double flMass = 0.0;
	std::string szDamping, szRotDamping, szInertia;
	std::ostringstream joints;
	joints.imbue( std::locale::classic() );
	for ( const kv1::CNode &root : parsed.m_Document.m_Roots )
	{
		if ( !root.IsBlock() )
			continue;
		const auto value = [ &root ]( std::string_view szKey ) {
			const kv1::CNode *pNode = root.Find( szKey );
			return pNode ? pNode->GetValue() : std::string{};
		};
		if ( Lower( root.GetKey() ) == "solid" )
		{
			const int nIndex = std::atoi( value( "index" ).c_str() );
			if ( nIndex >= 0 && static_cast<std::size_t>( nIndex ) < names.size() )
				names[ static_cast<std::size_t>( nIndex ) ] = value( "name" );
			flMass += std::atof( value( "mass" ).c_str() );
			if ( szDamping.empty() )
			{
				szDamping = value( "damping" );
				szRotDamping = value( "rotdamping" );
				szInertia = value( "inertia" );
			}
		}
	}
	for ( const kv1::CNode &root : parsed.m_Document.m_Roots )
	{
		if ( !root.IsBlock() || Lower( root.GetKey() ) != "ragdollconstraint" )
			continue;
		const auto value = [ &root ]( std::string_view szKey ) {
			const kv1::CNode *pNode = root.Find( szKey );
			return pNode ? pNode->GetValue() : std::string{ "0" };
		};
		const int nChild = std::atoi( value( "child" ).c_str() );
		if ( nChild < 0 || static_cast<std::size_t>( nChild ) >= names.size() || names[ static_cast<std::size_t>( nChild ) ].empty() )
			continue;
		for ( const char *pszAxis : { "x", "y", "z" } )
		{
			const std::string szAxis{ pszAxis };
			joints << "\t$jointconstrain \"" << names[ static_cast<std::size_t>( nChild ) ] << "\" " << szAxis << " limit " << value( szAxis + "min" ) << ' ' << value( szAxis + "max" ) << ' ' << value( szAxis + "friction" ) << '\n';
		}
	}
	std::ostringstream out;
	out.imbue( std::locale::classic() );
	out << "$collisionjoints \"collision.smd\"\n{\n\t$mass " << ( flMass > 0.0 ? flMass : 85.0 ) << '\n';
	if ( !szInertia.empty() )
		out << "\t$inertia " << szInertia << '\n';
	if ( !szDamping.empty() )
		out << "\t$damping " << szDamping << '\n';
	if ( !szRotDamping.empty() )
		out << "\t$rotdamping " << szRotDamping << '\n';
	const bool bPelvis = std::ranges::any_of( model.m_Bones, []( const Bone_t &bone ) { return bone.m_szName == "ValveBiped.Bip01_Pelvis"; } );
	if ( bPelvis )
		out << "\t$rootbone \"ValveBiped.Bip01_Pelvis\"\n";
	out << joints.str() << "}\n";
	return out.str();
}

//-----------------------------------------------------------------------------
// Purpose: The first line of text starting with szStart cut out (with its newline); whether
//          there was one.
//-----------------------------------------------------------------------------
bool CutLine( std::string &szText, std::string_view szStart )
{
	for ( std::size_t nAt = 0; nAt < szText.size(); )
	{
		const std::size_t nEnd = szText.find( '\n', nAt );
		const std::size_t nNext = nEnd == std::string::npos ? szText.size() : nEnd + 1;
		if ( std::string_view{ szText }.substr( nAt ).starts_with( szStart ) )
		{
			szText.erase( nAt, nNext - nAt );
			return true;
		}
		nAt = nNext;
	}
	return false;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::string> MissingPlayerBones( const CModel &model )
{
	std::vector<std::string> missing;
	for ( const std::string_view szBone : PLAYER_BONES )
	{
		const bool bHas = std::ranges::any_of( model.m_Bones, [ szBone ]( const Bone_t &bone ) { return Lower( bone.m_szName ) == Lower( szBone ); } );
		if ( !bHas )
			missing.emplace_back( szBone );
	}
	return missing;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view PlayerAnimationModel( PlayerAnimations_t nAnimations )
{
	switch ( nAnimations )
	{
		case PlayerAnimations_t::PLAYER_ANIMATIONS_FEMALE: return "f_anm.mdl";
		case PlayerAnimations_t::PLAYER_ANIMATIONS_ZOMBIE: return "z_anm.mdl";
		case PlayerAnimations_t::PLAYER_ANIMATIONS_MALE: break;
	}
	return "m_anm.mdl";
}

//-----------------------------------------------------------------------------
// Purpose: The source project made for models/player/, its ragdoll from the .phy given as
//          the collision, then its QC changed: no static prop, GMod's animations included, the
//          only sequence of its own the ragdoll pose, and the ragdoll's joints as the .phy had them.
//-----------------------------------------------------------------------------
Export_t ExportPlayermodel( std::string_view szPath, const Reader_t &pfnRead, const PlayermodelOptions_t &options )
{
	std::string szName = Lower( options.m_szName );
	std::erase_if( szName, []( char ch ) { return !std::isalnum( static_cast<unsigned char>( ch ) ) && ch != '_' && ch != '-'; } );
	if ( szName.empty() )
		throw std::runtime_error( "Give the playermodel a name (letters, digits, _ and -)" );
	std::string szError;
	const std::optional<CModel> model = LoadModel( szPath, pfnRead, &szError );
	if ( !model )
		throw std::runtime_error( szError );
	if ( const std::vector<std::string> missing = MissingPlayerBones( *model ); !missing.empty() )
	{
		std::string szMissing;
		for ( const std::string &szBone : missing )
			szMissing += ( szMissing.empty() ? "" : ", " ) + szBone;
		throw std::runtime_error( "Not on the ValveBiped skeleton GMod's player animations move; it lacks " + szMissing );
	}

	// The ragdoll: the .phy beside the model, when it has more than one piece.
	std::string szBase{ szPath };
	if ( szBase.size() > 4 )
		szBase.resize( szBase.size() - 4 );
	std::optional<Physics_t> physics;
	if ( const auto bytes = pfnRead( szBase + ".phy" ) )
		physics = ReadPhysics( *bytes );
	std::optional<CModel> ragdoll;
	if ( physics && physics->m_nSolids > 1 )
		ragdoll = RagdollMesh( *model, *physics );

	SourceOptions_t source;
	const std::string szModel = "player/" + szName + ".mdl";
	source.m_szModelName = szModel;
	if ( ragdoll )
		source.m_pCollision = &*ragdoll;
	Export_t result = ExportSourceProject( szPath, pfnRead, source );

	const Bytes_t &qcBytes = result.m_Files.at( "model.qc" );
	std::string qc{ reinterpret_cast<const char *>( qcBytes.data() ), qcBytes.size() };
	CutLine( qc, "$staticprop" );
	if ( CutLine( qc, "$sequence idle \"reference.smd\"" ) )
		qc += "$sequence \"ragdoll\" \"reference.smd\" fps 30 activity \"ACT_DIERAGDOLL\" 1\n";
	if ( const std::size_t nAt = qc.find( "$surfaceprop" ); nAt != std::string::npos )
	{
		const std::size_t nLine = qc.find( '\n', nAt );
		qc.insert( nLine == std::string::npos ? qc.size() : nLine + 1, "$includemodel \"" + std::string{ PlayerAnimationModel( options.m_nAnimations ) } + "\"\n" );
	}
	if ( ragdoll )
	{
		const std::size_t nAt = qc.find( "$collisionmodel \"collision.smd\"" );
		const std::size_t nEnd = nAt == std::string::npos ? std::string::npos : qc.find( "}\n", nAt );
		if ( nEnd != std::string::npos )
			qc.replace( nAt, nEnd + 2 - nAt, RagdollJoints( *physics, *model ) );
	}
	result.m_Files[ "model.qc" ] = Bytes_t{ reinterpret_cast<const std::byte *>( qc.data() ), reinterpret_cast<const std::byte *>( qc.data() + qc.size() ) };

	const std::string szDisplay = options.m_szDisplayName.empty() ? szName : options.m_szDisplayName;
	std::ostringstream lua;
	lua << "-- Lists models/" << szModel << " in Garry's Mod's player model menu.\n"
		<< "player_manager.AddValidModel( " << LuaString( szDisplay ) << ", " << LuaString( "models/" + szModel ) << " )\n"
		<< "player_manager.AddValidHands( " << LuaString( szDisplay ) << ", " << LuaString( options.m_szHands ) << ", 0, \"00000000\" )\n"
		<< "list.Set( \"PlayerOptionsModel\", " << LuaString( szDisplay ) << ", " << LuaString( "models/" + szModel ) << " )\n";
	const std::string szLua = lua.str();
	result.m_Files[ "lua/autorun/" + szName + "_playermodel.lua" ] = Bytes_t{ reinterpret_cast<const std::byte *>( szLua.data() ), reinterpret_cast<const std::byte *>( szLua.data() + szLua.size() ) };

	result.m_Notes.push_back( "Compile model.qc with studiomdl against Garry's Mod (it needs " + std::string{ PlayerAnimationModel( options.m_nAnimations ) } + " from it), then put models/" + szModel + " and lua/ in an addon folder." );
	if ( !ragdoll )
		result.m_Notes.push_back( "The model has no ragdoll (.phy with several pieces): it will fall as one piece. Add $collisionjoints to model.qc for one." );
	result.m_Notes.push_back( "Flexes (facial expressions) and eyes aren't carried over yet." );
	std::string szNotes;
	for ( const std::string &szNote : result.m_Notes )
		szNotes += szNote + '\n';
	result.m_Files[ "export-notes.txt" ] = Bytes_t{ reinterpret_cast<const std::byte *>( szNotes.data() ), reinterpret_cast<const std::byte *>( szNotes.data() + szNotes.size() ) };
	return result;
}

} // namespace xv::model
