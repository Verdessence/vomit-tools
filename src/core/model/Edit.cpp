//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Edit.h. Layout facts (offsets, record sizes, what a field means) are from the VDC
//          MDL and PHY articles and the public SDK headers, read for facts only.
//
//===========================================================================//
#include "Edit.h"

#include <cstring>
#include <optional>
#include <stdexcept>

#include "Binary.h"

namespace xv::model
{

namespace
{

constexpr std::int32_t MDL_ID = 0x54534449; // "IDST"
constexpr std::size_t HEADER_SIZE = 408;
constexpr std::size_t HEADER_LENGTH = 76;

// Where each list's count and offset are in the header.
constexpr std::size_t BONES = 156;
constexpr std::size_t CONTROLLERS = 164;
constexpr std::size_t HITBOX_SETS = 172;
constexpr std::size_t ANIMATIONS = 180;
constexpr std::size_t SEQUENCES = 188;
constexpr std::size_t TEXTURES = 204;
constexpr std::size_t FOLDERS = 212;
constexpr std::size_t SKIN_SLOTS = 220;
constexpr std::size_t SKIN_FAMILIES = 224;
constexpr std::size_t SKIN_TABLE = 228;
constexpr std::size_t BODYGROUPS = 232;
constexpr std::size_t ATTACHMENTS = 240;
constexpr std::size_t FLEXES = 260;
constexpr std::size_t POSE_PARAMETERS = 300;
constexpr std::size_t SURFACE_PROP = 308;
constexpr std::size_t KEY_VALUES = 312; // the offset, then the size
constexpr std::size_t MASS = 328;
constexpr std::size_t CONTENTS = 332;
constexpr std::size_t INCLUDES = 336;
constexpr std::size_t ANIMATION_FILE = 348;

constexpr std::size_t BONE_SIZE = 216;
constexpr std::size_t CONTROLLER_SIZE = 56;
constexpr std::size_t HITBOX_SET_SIZE = 12;
constexpr std::size_t HITBOX_SIZE = 68;
constexpr std::size_t ANIMATION_SIZE = 100;
constexpr std::size_t SEQUENCE_SIZE = 212;
constexpr std::size_t EVENT_SIZE = 80;
constexpr std::size_t TEXTURE_SIZE = 64;
constexpr std::size_t BODYGROUP_SIZE = 16;
constexpr std::size_t MODEL_SIZE = 148;
constexpr std::size_t MESH_SIZE = 116;
constexpr std::size_t ATTACHMENT_SIZE = 92;
constexpr std::size_t POSE_PARAMETER_SIZE = 20;
constexpr std::size_t INCLUDE_SIZE = 8;

constexpr std::size_t MAX_NAME = 1024;
constexpr std::size_t EVENT_OPTIONS = 64;
constexpr std::size_t FIXED_NAME = 64;

//-----------------------------------------------------------------------------
// Purpose: Checked reads and writes on a .mdl's bytes, and the end-of-file area moved things go.
//-----------------------------------------------------------------------------
class CMdl
{
public:
	explicit CMdl( Bytes_t &bytes )
		: m_Bytes( bytes )
	{
		View().Check( 0, HEADER_SIZE );
		if ( Int( 0 ) != MDL_ID )
			throw std::runtime_error( "Not a Source studio model" );
		if ( Int( 4 ) < 44 || Int( 4 ) > 49 )
			throw std::runtime_error( "Editing supports model versions 44–49" );
		if ( static_cast<std::size_t>( Int( HEADER_LENGTH ) ) != m_Bytes.size() )
			throw std::runtime_error( "The MDL's declared length does not match its file" );
	}

	[[nodiscard]] CBinary View() const { return CBinary( m_Bytes ); }
	[[nodiscard]] int Version() const { return Int( 4 ); }
	[[nodiscard]] int Int( std::size_t nAt ) const { return View().Int( nAt ); }
	[[nodiscard]] float Float( std::size_t nAt ) const { return View().Read<float>( nAt ); }
	[[nodiscard]] std::string Name( std::size_t nBase, std::size_t nField ) const { return View().Name( nBase, nField ); }

	template <class T>
	void Put( std::size_t nAt, T value )
	{
		View().Check( nAt, sizeof( T ) );
		std::memcpy( m_Bytes.data() + nAt, &value, sizeof( T ) );
	}
	void PutFloat( std::size_t nAt, float flValue )
	{
		if ( !std::isfinite( flValue ) )
			throw std::runtime_error( "Only finite numbers can be stored" );
		Put<float>( nAt, flValue );
	}
	void PutVector( std::size_t nAt, CVec3 vec )
	{
		PutFloat( nAt, vec.m_flX );
		PutFloat( nAt + 4, vec.m_flY );
		PutFloat( nAt + 8, vec.m_flZ );
	}
	void PutTransform( std::size_t nAt, const CTransform &transform )
	{
		for ( std::size_t i = 0; i < 12; i++ )
			PutFloat( nAt + i * 4, transform.m_Values[ i ] );
	}
	void PutFixedText( std::size_t nAt, std::size_t nSize, const std::string &szText )
	{
		if ( szText.size() >= nSize )
			throw std::runtime_error( "\"" + szText + "\" is longer than the " + std::to_string( nSize - 1 ) + " characters the model keeps for it" );
		View().Check( nAt, nSize );
		std::memset( m_Bytes.data() + nAt, 0, nSize );
		std::memcpy( m_Bytes.data() + nAt, szText.data(), szText.size() );
	}

	// The record nIndex of a list whose count is at nField and offset (from nBase) nGap bytes on.
	[[nodiscard]] std::size_t Record( std::size_t nBase, std::size_t nField, std::size_t nStride, int nIndex, const char *pszWhat, std::size_t nGap = 4 ) const
	{
		const int nCount = Int( nField );
		if ( nIndex < 0 || nIndex >= nCount )
			throw std::runtime_error( std::string( "There is no " ) + pszWhat + " " + std::to_string( nIndex ) );
		const auto nFirst = View().Relative( nBase, nField + nGap );
		View().Array( nFirst, nCount, nStride );
		return nFirst + static_cast<std::size_t>( nIndex ) * nStride;
	}
	[[nodiscard]] int Count( std::size_t nField ) const
	{
		const int nCount = Int( nField );
		if ( nCount < 0 || nCount > ( 1 << 20 ) )
			throw std::runtime_error( "A model count is out of range" );
		return nCount;
	}

	// Adds bytes at the end (aligned), keeping the declared length right; gives where they went.
	std::size_t Append( std::span<const std::byte> bytes, std::size_t nAlign = 4 )
	{
		const std::size_t nAt = ( m_Bytes.size() + nAlign - 1 ) / nAlign * nAlign;
		if ( nAt + bytes.size() > 0x7fffffff )
			throw std::runtime_error( "The model would grow past 2 GB" );
		m_Bytes.resize( nAt, std::byte{ 0 } );
		m_Bytes.insert( m_Bytes.end(), bytes.begin(), bytes.end() );
		Put<std::int32_t>( HEADER_LENGTH, static_cast<std::int32_t>( m_Bytes.size() ) );
		return nAt;
	}
	std::size_t AppendZeros( std::size_t nSize )
	{
		const Bytes_t zeros( nSize );
		return Append( zeros );
	}
	// A name added at the end, or one this edit already added.
	std::size_t AppendString( const std::string &szText )
	{
		// Lists written again keep their names as the file had them, odd characters and all.
		if ( szText.size() > MAX_NAME || szText.find( '\0' ) != std::string::npos )
			throw std::runtime_error( "A name is too long or holds a null character" );
		if ( const auto found = m_Strings.find( szText ); found != m_Strings.end() )
			return found->second;
		Bytes_t bytes( szText.size() + 1 );
		std::memcpy( bytes.data(), szText.data(), szText.size() );
		const std::size_t nAt = Append( bytes, 1 );
		m_Strings[ szText ] = nAt;
		return nAt;
	}
	// Points a record's name field at the name (relative to nBase), unless it already reads so;
	// nWriteBase, when given, is what a new offset counts from instead.
	void SetName( std::size_t nBase, std::size_t nField, const std::string &szText, std::optional<std::size_t> nWriteBase = std::nullopt )
	{
		CheckName( szText );
		if ( Int( nField ) != 0 && Name( nBase, nField ) == szText )
			return;
		PutOffset( nWriteBase.value_or( nBase ), nField, AppendString( szText ) );
	}
	void PutOffset( std::size_t nBase, std::size_t nField, std::size_t nTarget )
	{
		Put<std::int32_t>( nField, static_cast<std::int32_t>( static_cast<std::int64_t>( nTarget ) - static_cast<std::int64_t>( nBase ) ) );
	}
	// Remembers where a name already is, so lists written again reuse it.
	void KnowString( std::size_t nAt )
	{
		try
		{
			m_Strings.try_emplace( View().Text( nAt, MAX_NAME + 1 ), nAt );
		}
		catch ( const std::runtime_error & )
		{
			// Not a name this edit can reuse; a new copy is written instead.
		}
	}
	[[nodiscard]] Bytes_t Copy( std::size_t nAt, std::size_t nSize ) const
	{
		View().Check( nAt, nSize );
		return Bytes_t( m_Bytes.begin() + static_cast<std::ptrdiff_t>( nAt ), m_Bytes.begin() + static_cast<std::ptrdiff_t>( nAt + nSize ) );
	}

	static void CheckName( const std::string &szText )
	{
		if ( szText.size() > MAX_NAME )
			throw std::runtime_error( "A name is longer than " + std::to_string( MAX_NAME ) + " characters" );
		for ( const char c : szText )
			if ( static_cast<unsigned char>( c ) < 0x20 )
				throw std::runtime_error( "Names can't hold control characters" );
	}

private:
	Bytes_t &m_Bytes;
	std::map<std::string, std::size_t> m_Strings;
};

//-----------------------------------------------------------------------------
// Purpose: Runs an edit on a copy, so a refused edit leaves the bytes as they were.
//-----------------------------------------------------------------------------
template <class F>
auto Guarded( Bytes_t &mdl, F &&pfnEdit )
{
	Bytes_t copy = mdl;
	CMdl file( copy );
	if constexpr ( std::is_void_v<decltype( pfnEdit( file ) )> )
	{
		pfnEdit( file );
		mdl.swap( copy );
	}
	else
	{
		auto result = pfnEdit( file );
		mdl.swap( copy );
		return result;
	}
}

//-----------------------------------------------------------------------------
// Purpose: The hitbox name's base (see CBinary::HitboxNameBase).
//-----------------------------------------------------------------------------
std::size_t HitboxNameBase( const CMdl &file, std::size_t nBox )
{
	return file.View().HitboxNameBase( file.Version(), nBox );
}

//-----------------------------------------------------------------------------
// Purpose: Where a name field is, for each kind of name.
//-----------------------------------------------------------------------------
struct NameField_t
{
	std::size_t m_nBase = 0, m_nField = 0;
	bool m_bFixed = false; // a char[64] in the record
	std::optional<std::size_t> m_nWriteBase; // what a new offset counts from, when not m_nBase
};
NameField_t FindName( const CMdl &file, Name_t nWhat, int nIndex, int nSub )
{
	switch ( nWhat )
	{
		case Name_t::NAME_BONE:
		{
			const auto n = file.Record( 0, BONES, BONE_SIZE, nIndex, "bone" );
			return { n, n, false, {} };
		}
		case Name_t::NAME_ATTACHMENT:
		{
			const auto n = file.Record( 0, ATTACHMENTS, ATTACHMENT_SIZE, nIndex, "attachment" );
			return { n, n, false, {} };
		}
		case Name_t::NAME_HITBOX_SET:
		{
			const auto n = file.Record( 0, HITBOX_SETS, HITBOX_SET_SIZE, nIndex, "hitbox set" );
			return { n, n, false, {} };
		}
		case Name_t::NAME_HITBOX:
		{
			const auto nSet = file.Record( 0, HITBOX_SETS, HITBOX_SET_SIZE, nSub, "hitbox set" );
			const auto n = file.Record( nSet, nSet + 4, HITBOX_SIZE, nIndex, "hitbox" );
			return { HitboxNameBase( file, n ), n + 32, false, n };
		}
		case Name_t::NAME_SEQUENCE:
		case Name_t::NAME_ACTIVITY:
		{
			const auto n = file.Record( 0, SEQUENCES, SEQUENCE_SIZE, nIndex, "sequence" );
			return { n, n + ( nWhat == Name_t::NAME_SEQUENCE ? 4 : 8 ), false, {} };
		}
		case Name_t::NAME_ANIMATION:
		{
			const auto n = file.Record( 0, ANIMATIONS, ANIMATION_SIZE, nIndex, "animation" );
			return { n, n + 4, false, {} };
		}
		case Name_t::NAME_BODYGROUP:
		{
			const auto n = file.Record( 0, BODYGROUPS, BODYGROUP_SIZE, nIndex, "bodygroup" );
			return { n, n, false, {} };
		}
		case Name_t::NAME_BODYGROUP_MODEL:
		{
			const auto nPart = file.Record( 0, BODYGROUPS, BODYGROUP_SIZE, nSub, "bodygroup" );
			const auto n = file.Record( nPart, nPart + 4, MODEL_SIZE, nIndex, "bodygroup model", 8 );
			return { n, n, true, {} };
		}
		case Name_t::NAME_TEXTURE:
		{
			const auto n = file.Record( 0, TEXTURES, TEXTURE_SIZE, nIndex, "texture" );
			return { n, n, false, {} };
		}
		case Name_t::NAME_POSE_PARAMETER:
		{
			const auto n = file.Record( 0, POSE_PARAMETERS, POSE_PARAMETER_SIZE, nIndex, "pose parameter" );
			return { n, n, false, {} };
		}
		case Name_t::NAME_EVENT:
		{
			const auto nSequence = file.Record( 0, SEQUENCES, SEQUENCE_SIZE, nSub, "sequence" );
			const auto n = file.Record( nSequence, nSequence + 24, EVENT_SIZE, nIndex, "event" );
			return { n, n + 76, false, {} };
		}
		case Name_t::NAME_FLEX:
		{
			const auto n = file.Record( 0, FLEXES, 4, nIndex, "flex" );
			return { n, n, false, {} };
		}
	}
	throw std::runtime_error( "Unknown kind of name" );
}

//-----------------------------------------------------------------------------
// Purpose: The skin table as rows of texture indices.
//-----------------------------------------------------------------------------
std::vector<std::vector<std::int16_t>> ReadSkins( const CMdl &file )
{
	const int nSlots = file.Int( SKIN_SLOTS ), nFamilies = file.Int( SKIN_FAMILIES );
	if ( nSlots < 0 || nSlots > 4096 || nFamilies < 0 || nFamilies > 4096 )
		throw std::runtime_error( "Invalid skin table" );
	std::vector<std::vector<std::int16_t>> skins( static_cast<std::size_t>( nFamilies ), std::vector<std::int16_t>( static_cast<std::size_t>( nSlots ) ) );
	if ( !nSlots || !nFamilies )
		return skins;
	const auto nAt = file.View().Relative( 0, SKIN_TABLE );
	file.View().Array( nAt, nSlots * nFamilies, 2 );
	for ( int i = 0; i < nFamilies; i++ )
		for ( int j = 0; j < nSlots; j++ )
			skins[ i ][ j ] = file.View().Read<std::int16_t>( nAt + static_cast<std::size_t>( i * nSlots + j ) * 2 );
	return skins;
}

//-----------------------------------------------------------------------------
// Purpose: Writes the skin table again at the end (its row count may change).
//-----------------------------------------------------------------------------
void WriteSkins( CMdl &file, const std::vector<std::vector<std::int16_t>> &skins )
{
	const int nSlots = file.Int( SKIN_SLOTS );
	Bytes_t table( skins.size() * static_cast<std::size_t>( nSlots ) * 2 );
	for ( std::size_t i = 0; i < skins.size(); i++ )
		std::memcpy( table.data() + i * static_cast<std::size_t>( nSlots ) * 2, skins[ i ].data(), static_cast<std::size_t>( nSlots ) * 2 );
	const auto nAt = file.Append( table );
	file.Put<std::int32_t>( SKIN_FAMILIES, static_cast<std::int32_t>( skins.size() ) );
	file.PutOffset( 0, SKIN_TABLE, nAt );
}

//-----------------------------------------------------------------------------
// Purpose: Copies a list's records to the end, adjusting each one's offsets that count from it so
//          they still reach what they did; gives where the copy starts.
//-----------------------------------------------------------------------------
std::size_t MoveRecords( CMdl &file, std::size_t nFirst, int nCount, std::size_t nStride, std::span<const std::size_t> selfRelative, int nExtra = 0 )
{
	Bytes_t records = file.Copy( nFirst, static_cast<std::size_t>( nCount ) * nStride );
	records.resize( static_cast<std::size_t>( nCount + nExtra ) * nStride );
	const auto nAt = file.Append( records );
	const auto nShift = static_cast<std::int64_t>( nAt ) - static_cast<std::int64_t>( nFirst );
	for ( int i = 0; i < nCount; i++ )
		for ( const std::size_t nField : selfRelative )
		{
			const auto nRecord = nAt + static_cast<std::size_t>( i ) * nStride;
			if ( const int nOffset = file.Int( nRecord + nField ) )
				file.Put<std::int32_t>( nRecord + nField, static_cast<std::int32_t>( nOffset - nShift ) );
		}
	return nAt;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Runs of differing bytes, joined when they're close so a change isn't thousands of runs.
//-----------------------------------------------------------------------------
Change_t Diff( const Files_t &before, const Files_t &after )
{
	constexpr std::size_t JOIN = 16;
	Change_t change;
	for ( const auto &[ szKind, newBytes ] : after )
	{
		const auto found = before.find( szKind );
		static const Bytes_t s_Empty;
		const Bytes_t &oldBytes = found == before.end() ? s_Empty : found->second;
		if ( oldBytes == newBytes )
			continue;
		FileChange_t file;
		file.m_nOldSize = oldBytes.size();
		file.m_nNewSize = newBytes.size();
		const std::size_t nCommon = std::min( oldBytes.size(), newBytes.size() );
		const std::size_t nEnd = std::max( oldBytes.size(), newBytes.size() );
		const auto run = [ & ]( std::size_t nFrom, std::size_t nTo ) {
			FileChange_t::Run_t r;
			r.m_nAt = nFrom;
			const auto slice = [ & ]( const Bytes_t &bytes ) {
				const std::size_t a = std::min( nFrom, bytes.size() ), b = std::min( nTo, bytes.size() );
				return Bytes_t( bytes.begin() + static_cast<std::ptrdiff_t>( a ), bytes.begin() + static_cast<std::ptrdiff_t>( b ) );
			};
			r.m_Old = slice( oldBytes );
			r.m_New = slice( newBytes );
			file.m_Runs.push_back( std::move( r ) );
		};
		std::size_t i = 0;
		while ( i < nCommon )
		{
			if ( oldBytes[ i ] == newBytes[ i ] )
			{
				i++;
				continue;
			}
			const std::size_t nStart = i;
			std::size_t nLast = i;
			while ( i < nCommon && i - nLast <= JOIN )
			{
				if ( oldBytes[ i ] != newBytes[ i ] )
					nLast = i;
				i++;
			}
			// A run reaching the shared end joins the tail.
			if ( i >= nCommon && nCommon < nEnd && nCommon - nLast <= JOIN )
			{
				run( nStart, nEnd );
				i = nEnd;
				break;
			}
			run( nStart, nLast + 1 );
		}
		if ( i < nEnd && nCommon < nEnd )
			run( nCommon, nEnd );
		change[ szKind ] = std::move( file );
	}
	return change;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void ApplyChange( Files_t &files, const Change_t &change, bool bForward )
{
	for ( const auto &[ szKind, file ] : change )
	{
		Bytes_t &bytes = files[ szKind ];
		bytes.resize( bForward ? file.m_nNewSize : file.m_nOldSize );
		for ( const auto &run : file.m_Runs )
		{
			const Bytes_t &data = bForward ? run.m_New : run.m_Old;
			std::copy( data.begin(), data.end(), bytes.begin() + static_cast<std::ptrdiff_t>( run.m_nAt ) );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetHeaderVector( Bytes_t &mdl, HeaderVector_t nWhich, CVec3 vecValue )
{
	Guarded( mdl, [ & ]( CMdl &file ) { file.PutVector( 80 + static_cast<std::size_t>( nWhich ) * 12, vecValue ); } );
}

void SetFlags( Bytes_t &mdl, int nFlags )
{
	Guarded( mdl, [ & ]( CMdl &file ) { file.Put<std::int32_t>( 152, nFlags ); } );
}

void SetContents( Bytes_t &mdl, int nContents )
{
	Guarded( mdl, [ & ]( CMdl &file ) { file.Put<std::int32_t>( CONTENTS, nContents ); } );
}

void SetMass( Bytes_t &mdl, float flMass )
{
	if ( !( flMass >= 0 ) )
		throw std::runtime_error( "Mass can't be negative" );
	Guarded( mdl, [ & ]( CMdl &file ) { file.PutFloat( MASS, flMass ); } );
}

void SetInternalName( Bytes_t &mdl, const std::string &szName )
{
	CMdl::CheckName( szName );
	Guarded( mdl, [ & ]( CMdl &file ) { file.PutFixedText( 12, FIXED_NAME, szName ); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetSurfaceProp( Bytes_t &mdl, const std::string &szName )
{
	Guarded( mdl, [ & ]( CMdl &file ) { file.SetName( 0, SURFACE_PROP, szName ); } );
}

//-----------------------------------------------------------------------------
// Purpose: Written where the old text was when it fits, else at the end; the size counts the
//          terminator.
//-----------------------------------------------------------------------------
void SetKeyValues( Bytes_t &mdl, const std::string &szText )
{
	if ( szText.find( '\0' ) != std::string::npos )
		throw std::runtime_error( "Keyvalues can't hold a null character" );
	Guarded( mdl, [ & ]( CMdl &file ) {
		const int nOld = file.Int( KEY_VALUES + 4 );
		if ( nOld > 0 )
		{
			const auto nAt = file.View().Relative( 0, KEY_VALUES );
			if ( file.View().FixedText( nAt, static_cast<std::size_t>( nOld ) ) == szText )
				return;
		}
		if ( szText.empty() )
		{
			file.Put<std::int32_t>( KEY_VALUES + 4, 0 );
			return;
		}
		Bytes_t text( szText.size() + 1 );
		std::memcpy( text.data(), szText.data(), szText.size() );
		if ( nOld > 0 && text.size() <= static_cast<std::size_t>( nOld ) )
		{
			const auto nAt = file.View().Relative( 0, KEY_VALUES );
			for ( std::size_t i = 0; i < static_cast<std::size_t>( nOld ); i++ )
				file.Put<std::byte>( nAt + i, i < text.size() ? text[ i ] : std::byte{ 0 } );
		}
		else
			file.PutOffset( 0, KEY_VALUES, file.Append( text, 1 ) );
		file.Put<std::int32_t>( KEY_VALUES + 4, static_cast<std::int32_t>( text.size() ) );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetAnimationFile( Bytes_t &mdl, const std::string &szPath )
{
	Guarded( mdl, [ & ]( CMdl &file ) {
		if ( !file.Int( ANIMATION_FILE ) )
			throw std::runtime_error( "The model keeps its animations inside the .mdl" );
		file.SetName( 0, ANIMATION_FILE, szPath );
	} );
}

//-----------------------------------------------------------------------------
// Purpose: Each include is a label (empty) and a path, both counted from its record.
//-----------------------------------------------------------------------------
void SetIncludes( Bytes_t &mdl, const std::vector<std::string> &includes )
{
	Guarded( mdl, [ & ]( CMdl &file ) {
		const int nOld = file.Count( INCLUDES );
		std::vector<std::string> current;
		for ( int i = 0; i < nOld; i++ )
		{
			const auto n = file.Record( 0, INCLUDES, INCLUDE_SIZE, i, "include" );
			current.push_back( file.Name( n, n + 4 ) );
		}
		if ( current == includes )
			return;
		const auto nAt = file.AppendZeros( includes.size() * INCLUDE_SIZE );
		for ( std::size_t i = 0; i < includes.size(); i++ )
		{
			if ( includes[ i ].empty() )
				throw std::runtime_error( "An include needs a path" );
			const auto n = nAt + i * INCLUDE_SIZE;
			file.PutOffset( n, n, file.AppendString( "" ) );
			file.PutOffset( n, n + 4, file.AppendString( includes[ i ] ) );
		}
		file.Put<std::int32_t>( INCLUDES, static_cast<std::int32_t>( includes.size() ) );
		file.PutOffset( 0, INCLUDES + 4, nAt );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void Rename( Bytes_t &mdl, Name_t nWhat, int nIndex, const std::string &szName, int nSub )
{
	Guarded( mdl, [ & ]( CMdl &file ) {
		const NameField_t field = FindName( file, nWhat, nIndex, nSub );
		if ( field.m_bFixed )
		{
			CMdl::CheckName( szName );
			file.PutFixedText( field.m_nField, FIXED_NAME, szName );
		}
		else
			file.SetName( field.m_nBase, field.m_nField, szName, field.m_nWriteBase );
	} );
}

std::string GetName( const Bytes_t &mdl, Name_t nWhat, int nIndex, int nSub )
{
	Bytes_t copy = mdl;
	const CMdl file( copy );
	const NameField_t field = FindName( file, nWhat, nIndex, nSub );
	return field.m_bFixed ? file.View().FixedText( field.m_nField, FIXED_NAME ) : file.Name( field.m_nBase, field.m_nField );
}

//-----------------------------------------------------------------------------
// Purpose: The folders are a list of offsets from the file's start.
//-----------------------------------------------------------------------------
void SetFolders( Bytes_t &mdl, const std::vector<std::string> &folders )
{
	Guarded( mdl, [ & ]( CMdl &file ) {
		const int nOld = file.Count( FOLDERS );
		std::vector<std::string> current;
		for ( int i = 0; i < nOld; i++ )
			current.push_back( file.Name( 0, file.Record( 0, FOLDERS, 4, i, "material folder" ) ) );
		if ( current == folders )
			return;
		const auto nAt = file.AppendZeros( folders.size() * 4 );
		for ( std::size_t i = 0; i < folders.size(); i++ )
			file.PutOffset( 0, nAt + i * 4, file.AppendString( folders[ i ] ) );
		file.Put<std::int32_t>( FOLDERS, static_cast<std::int32_t>( folders.size() ) );
		file.PutOffset( 0, FOLDERS + 4, nAt );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int AddTexture( Bytes_t &mdl, const std::string &szName )
{
	if ( szName.empty() )
		throw std::runtime_error( "A texture needs a name" );
	return Guarded( mdl, [ & ]( CMdl &file ) {
		const int nCount = file.Count( TEXTURES );
		if ( nCount >= 32767 )
			throw std::runtime_error( "The skin table can't reach more textures" );
		const std::size_t nFirst = nCount ? file.View().Relative( 0, TEXTURES + 4 ) : 0;
		file.View().Array( nFirst, nCount, TEXTURE_SIZE );
		constexpr std::size_t FIELDS[] = { 0 };
		const auto nAt = MoveRecords( file, nFirst, nCount, TEXTURE_SIZE, FIELDS, 1 );
		const auto nNew = nAt + static_cast<std::size_t>( nCount ) * TEXTURE_SIZE;
		file.PutOffset( nNew, nNew, file.AppendString( szName ) );
		file.Put<std::int32_t>( TEXTURES, nCount + 1 );
		file.PutOffset( 0, TEXTURES + 4, nAt );
		return nCount;
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void RemoveTexture( Bytes_t &mdl, int nTexture )
{
	Guarded( mdl, [ & ]( CMdl &file ) {
		const auto nRecord = file.Record( 0, TEXTURES, TEXTURE_SIZE, nTexture, "texture" );
		auto skins = ReadSkins( file );
		for ( std::size_t i = 0; i < skins.size(); i++ )
			for ( std::size_t j = 0; j < skins[ i ].size(); j++ )
			{
				if ( skins[ i ][ j ] == nTexture )
					throw std::runtime_error( "Skin " + std::to_string( i ) + " still uses it in slot " + std::to_string( j ) );
				if ( skins[ i ][ j ] > nTexture )
					skins[ i ][ j ]--;
			}
		const int nCount = file.Count( TEXTURES );
		const auto nFirst = nRecord - static_cast<std::size_t>( nTexture ) * TEXTURE_SIZE;
		constexpr std::size_t FIELDS[] = { 0 };
		const auto nAt = MoveRecords( file, nFirst, nCount, TEXTURE_SIZE, FIELDS );
		// Close the gap; the names count from each record, so they move with it.
		for ( int i = nTexture; i + 1 < nCount; i++ )
		{
			const auto nTo = nAt + static_cast<std::size_t>( i ) * TEXTURE_SIZE;
			Bytes_t next = file.Copy( nTo + TEXTURE_SIZE, TEXTURE_SIZE );
			const int nName = file.View().Read<std::int32_t>( nTo + TEXTURE_SIZE );
			for ( std::size_t k = 0; k < TEXTURE_SIZE; k++ )
				file.Put<std::byte>( nTo + k, next[ k ] );
			if ( nName )
				file.Put<std::int32_t>( nTo, nName + static_cast<std::int32_t>( TEXTURE_SIZE ) );
		}
		file.Put<std::int32_t>( TEXTURES, nCount - 1 );
		file.PutOffset( 0, TEXTURES + 4, nAt );
		if ( !skins.empty() && file.Int( SKIN_SLOTS ) > 0 )
			WriteSkins( file, skins );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetSkinTexture( Bytes_t &mdl, int nFamily, int nSlot, int nTexture )
{
	Guarded( mdl, [ & ]( CMdl &file ) {
		const int nSlots = file.Int( SKIN_SLOTS ), nFamilies = file.Int( SKIN_FAMILIES );
		if ( nFamily < 0 || nFamily >= nFamilies || nSlot < 0 || nSlot >= nSlots )
			throw std::runtime_error( "There is no such skin slot" );
		if ( nTexture < 0 || nTexture >= file.Count( TEXTURES ) )
			throw std::runtime_error( "There is no texture " + std::to_string( nTexture ) );
		const auto nAt = file.View().Relative( 0, SKIN_TABLE );
		file.View().Array( nAt, nSlots * nFamilies, 2 );
		file.Put<std::int16_t>( nAt + static_cast<std::size_t>( nFamily * nSlots + nSlot ) * 2, static_cast<std::int16_t>( nTexture ) );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void AddSkinFamily( Bytes_t &mdl, int nFrom )
{
	Guarded( mdl, [ & ]( CMdl &file ) {
		auto skins = ReadSkins( file );
		if ( skins.empty() || file.Int( SKIN_SLOTS ) <= 0 )
			throw std::runtime_error( "The model has no skin slots to copy" );
		if ( skins.size() >= 4096 )
			throw std::runtime_error( "Too many skins" );
		const std::size_t nCopy = nFrom < 0 ? 0 : static_cast<std::size_t>( nFrom );
		if ( nCopy >= skins.size() )
			throw std::runtime_error( "There is no skin " + std::to_string( nFrom ) );
		skins.push_back( skins[ nCopy ] );
		WriteSkins( file, skins );
	} );
}

void RemoveSkinFamily( Bytes_t &mdl, int nFamily )
{
	Guarded( mdl, [ & ]( CMdl &file ) {
		auto skins = ReadSkins( file );
		if ( nFamily <= 0 || nFamily >= static_cast<int>( skins.size() ) )
			throw std::runtime_error( "Only added skins (1 and later) can be removed" );
		skins.erase( skins.begin() + nFamily );
		// Fewer rows fit where the table is.
		const int nSlots = file.Int( SKIN_SLOTS );
		const auto nAt = file.View().Relative( 0, SKIN_TABLE );
		for ( std::size_t i = 0; i < skins.size(); i++ )
			for ( int j = 0; j < nSlots; j++ )
				file.Put<std::int16_t>( nAt + ( i * static_cast<std::size_t>( nSlots ) + static_cast<std::size_t>( j ) ) * 2, skins[ i ][ j ] );
		file.Put<std::int32_t>( SKIN_FAMILIES, static_cast<std::int32_t>( skins.size() ) );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetMeshMaterial( Bytes_t &mdl, int nBodygroup, int nModel, int nMesh, int nSlot )
{
	Guarded( mdl, [ & ]( CMdl &file ) {
		if ( nSlot < 0 || nSlot >= file.Int( SKIN_SLOTS ) )
			throw std::runtime_error( "There is no skin slot " + std::to_string( nSlot ) );
		const auto nPart = file.Record( 0, BODYGROUPS, BODYGROUP_SIZE, nBodygroup, "bodygroup" );
		const auto nModelAt = file.Record( nPart, nPart + 4, MODEL_SIZE, nModel, "bodygroup model", 8 );
		const auto nMeshAt = file.Record( nModelAt, nModelAt + 72, MESH_SIZE, nMesh, "mesh" );
		file.Put<std::int32_t>( nMeshAt, nSlot );
	} );
}

std::vector<MeshRef_t> ListMeshes( const Bytes_t &mdl )
{
	Bytes_t copy = mdl;
	const CMdl file( copy );
	std::vector<MeshRef_t> meshes;
	const int nParts = file.Count( BODYGROUPS );
	for ( int p = 0; p < nParts; p++ )
	{
		const auto nPart = file.Record( 0, BODYGROUPS, BODYGROUP_SIZE, p, "bodygroup" );
		const int nModels = file.Count( nPart + 4 );
		for ( int m = 0; m < nModels; m++ )
		{
			const auto nModel = file.Record( nPart, nPart + 4, MODEL_SIZE, m, "bodygroup model", 8 );
			const int nMeshes = file.Count( nModel + 72 );
			for ( int k = 0; k < nMeshes; k++ )
			{
				const auto nMesh = file.Record( nModel, nModel + 72, MESH_SIZE, k, "mesh" );
				meshes.push_back( { p, m, k, file.Int( nMesh ), file.View().FixedText( nModel, FIXED_NAME ) } );
			}
		}
	}
	return meshes;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetHitbox( Bytes_t &mdl, int nSet, int nBox, const Hitbox_t &box )
{
	Guarded( mdl, [ & ]( CMdl &file ) {
		if ( box.m_nBone < 0 || box.m_nBone >= file.Count( BONES ) )
			throw std::runtime_error( "There is no bone " + std::to_string( box.m_nBone ) );
		const auto nSetAt = file.Record( 0, HITBOX_SETS, HITBOX_SET_SIZE, nSet, "hitbox set" );
		const auto n = file.Record( nSetAt, nSetAt + 4, HITBOX_SIZE, nBox, "hitbox" );
		file.Put<std::int32_t>( n, box.m_nBone );
		file.Put<std::int32_t>( n + 4, box.m_nGroup );
		file.PutVector( n + 8, box.m_vecMin );
		file.PutVector( n + 20, box.m_vecMax );
		if ( !box.m_szName.empty() || file.Int( n + 32 ) )
			file.SetName( HitboxNameBase( file, n ), n + 32, box.m_szName, n );
	} );
}

//-----------------------------------------------------------------------------
// Purpose: The set's boxes written again at the end; names the file has are reused.
//-----------------------------------------------------------------------------
void SetHitboxes( Bytes_t &mdl, int nSet, const std::vector<Hitbox_t> &boxes )
{
	Guarded( mdl, [ & ]( CMdl &file ) {
		const auto nSetAt = file.Record( 0, HITBOX_SETS, HITBOX_SET_SIZE, nSet, "hitbox set" );
		const int nOld = file.Count( nSetAt + 4 );
		const int nBones = file.Count( BONES );
		for ( int i = 0; i < nOld; i++ )
		{
			const auto n = file.Record( nSetAt, nSetAt + 4, HITBOX_SIZE, i, "hitbox" );
			if ( file.Int( n + 32 ) )
				file.KnowString( file.View().Relative( HitboxNameBase( file, n ), n + 32 ) );
		}
		const auto nAt = file.AppendZeros( boxes.size() * HITBOX_SIZE );
		for ( std::size_t i = 0; i < boxes.size(); i++ )
		{
			const Hitbox_t &box = boxes[ i ];
			if ( box.m_nBone < 0 || box.m_nBone >= nBones )
				throw std::runtime_error( "There is no bone " + std::to_string( box.m_nBone ) );
			const auto n = nAt + i * HITBOX_SIZE;
			file.Put<std::int32_t>( n, box.m_nBone );
			file.Put<std::int32_t>( n + 4, box.m_nGroup );
			file.PutVector( n + 8, box.m_vecMin );
			file.PutVector( n + 20, box.m_vecMax );
			file.PutOffset( n, n + 32, file.AppendString( box.m_szName ) );
		}
		file.Put<std::int32_t>( nSetAt + 4, static_cast<std::int32_t>( boxes.size() ) );
		file.PutOffset( nSetAt, nSetAt + 8, nAt );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetAttachment( Bytes_t &mdl, int nAttachment, int nBone, const CTransform &local )
{
	Guarded( mdl, [ & ]( CMdl &file ) {
		if ( nBone < 0 || nBone >= file.Count( BONES ) )
			throw std::runtime_error( "There is no bone " + std::to_string( nBone ) );
		const auto n = file.Record( 0, ATTACHMENTS, ATTACHMENT_SIZE, nAttachment, "attachment" );
		file.Put<std::int32_t>( n + 8, nBone );
		file.PutTransform( n + 12, local );
	} );
}

std::vector<Attachment_t> ReadAttachments( const Bytes_t &mdl )
{
	Bytes_t copy = mdl;
	const CMdl file( copy );
	std::vector<Attachment_t> attachments;
	const int nCount = file.Count( ATTACHMENTS );
	for ( int i = 0; i < nCount; i++ )
	{
		const auto n = file.Record( 0, ATTACHMENTS, ATTACHMENT_SIZE, i, "attachment" );
		Attachment_t attachment;
		attachment.m_szName = file.Name( n, n );
		attachment.m_nFlags = file.Int( n + 4 );
		attachment.m_nBone = file.Int( n + 8 );
		for ( std::size_t k = 0; k < 12; k++ )
			attachment.m_Local.m_Values[ k ] = file.Float( n + 12 + k * 4 );
		attachments.push_back( std::move( attachment ) );
	}
	return attachments;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetAttachments( Bytes_t &mdl, const std::vector<Attachment_t> &attachments )
{
	Guarded( mdl, [ & ]( CMdl &file ) {
		const int nOld = file.Count( ATTACHMENTS );
		for ( int i = 0; i < nOld; i++ )
		{
			const auto n = file.Record( 0, ATTACHMENTS, ATTACHMENT_SIZE, i, "attachment" );
			if ( file.Int( n ) )
				file.KnowString( file.View().Relative( n, n ) );
		}
		const int nBones = file.Count( BONES );
		const auto nAt = file.AppendZeros( attachments.size() * ATTACHMENT_SIZE );
		for ( std::size_t i = 0; i < attachments.size(); i++ )
		{
			const Attachment_t &attachment = attachments[ i ];
			if ( attachment.m_nBone < 0 || attachment.m_nBone >= nBones )
				throw std::runtime_error( "There is no bone " + std::to_string( attachment.m_nBone ) );
			if ( attachment.m_szName.empty() )
				throw std::runtime_error( "An attachment needs a name" );
			const auto n = nAt + i * ATTACHMENT_SIZE;
			file.PutOffset( n, n, file.AppendString( attachment.m_szName ) );
			file.Put<std::int32_t>( n + 4, attachment.m_nFlags );
			file.Put<std::int32_t>( n + 8, attachment.m_nBone );
			file.PutTransform( n + 12, attachment.m_Local );
		}
		file.Put<std::int32_t>( ATTACHMENTS, static_cast<std::int32_t>( attachments.size() ) );
		file.PutOffset( 0, ATTACHMENTS + 4, nAt );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetSequence( Bytes_t &mdl, int nSequence, const SequenceFields_t &fields )
{
	Guarded( mdl, [ & ]( CMdl &file ) {
		const auto n = file.Record( 0, SEQUENCES, SEQUENCE_SIZE, nSequence, "sequence" );
		if ( !( fields.m_flFadeIn >= 0 ) || !( fields.m_flFadeOut >= 0 ) )
			throw std::runtime_error( "Fade times can't be negative" );
		const int nFlags = ( file.Int( n + 12 ) & ~SEQUENCE_EDITABLE_FLAGS ) | ( fields.m_nFlags & SEQUENCE_EDITABLE_FLAGS );
		file.Put<std::int32_t>( n + 12, nFlags );
		file.Put<std::int32_t>( n + 20, fields.m_nWeight );
		file.PutFloat( n + 104, fields.m_flFadeIn );
		file.PutFloat( n + 108, fields.m_flFadeOut );
	} );
}

void SetAnimationFps( Bytes_t &mdl, int nAnimation, float flFps )
{
	if ( !( flFps > 0 ) || flFps > 1000 )
		throw std::runtime_error( "Frame rates are between 0 and 1000" );
	Guarded( mdl, [ & ]( CMdl &file ) { file.PutFloat( file.Record( 0, ANIMATIONS, ANIMATION_SIZE, nAnimation, "animation" ) + 8, flFps ); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetEvent( Bytes_t &mdl, int nSequence, int nEvent, const Event_t &event )
{
	Guarded( mdl, [ & ]( CMdl &file ) {
		if ( !( event.m_flCycle >= 0 && event.m_flCycle <= 1 ) )
			throw std::runtime_error( "An event's cycle is between 0 and 1" );
		const auto nSeq = file.Record( 0, SEQUENCES, SEQUENCE_SIZE, nSequence, "sequence" );
		const auto n = file.Record( nSeq, nSeq + 24, EVENT_SIZE, nEvent, "event" );
		file.PutFloat( n, event.m_flCycle );
		file.Put<std::int32_t>( n + 4, event.m_nEvent );
		file.Put<std::int32_t>( n + 8, event.m_nType );
		file.PutFixedText( n + 12, EVENT_OPTIONS, event.m_szOptions );
		if ( !event.m_szName.empty() || file.Int( n + 76 ) )
			file.SetName( n, n + 76, event.m_szName );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetEvents( Bytes_t &mdl, int nSequence, std::vector<Event_t> events )
{
	std::ranges::stable_sort( events, {}, &Event_t::m_flCycle );
	Guarded( mdl, [ & ]( CMdl &file ) {
		const auto nSeq = file.Record( 0, SEQUENCES, SEQUENCE_SIZE, nSequence, "sequence" );
		const int nOld = file.Count( nSeq + 24 );
		for ( int i = 0; i < nOld; i++ )
		{
			const auto n = file.Record( nSeq, nSeq + 24, EVENT_SIZE, i, "event" );
			if ( file.Int( n + 76 ) )
				file.KnowString( file.View().Relative( n, n + 76 ) );
		}
		const auto nAt = file.AppendZeros( events.size() * EVENT_SIZE );
		for ( std::size_t i = 0; i < events.size(); i++ )
		{
			const Event_t &event = events[ i ];
			if ( !( event.m_flCycle >= 0 && event.m_flCycle <= 1 ) )
				throw std::runtime_error( "An event's cycle is between 0 and 1" );
			const auto n = nAt + i * EVENT_SIZE;
			file.PutFloat( n, event.m_flCycle );
			file.Put<std::int32_t>( n + 4, event.m_nEvent );
			file.Put<std::int32_t>( n + 8, event.m_nType );
			file.PutFixedText( n + 12, EVENT_OPTIONS, event.m_szOptions );
			file.PutOffset( n, n + 76, file.AppendString( event.m_szName ) );
		}
		file.Put<std::int32_t>( nSeq + 24, static_cast<std::int32_t>( events.size() ) );
		file.PutOffset( nSeq, nSeq + 28, nAt );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetController( Bytes_t &mdl, int nController, float flStart, float flEnd )
{
	Guarded( mdl, [ & ]( CMdl &file ) {
		const auto n = file.Record( 0, CONTROLLERS, CONTROLLER_SIZE, nController, "bone controller" );
		file.PutFloat( n + 8, flStart );
		file.PutFloat( n + 12, flEnd );
	} );
}

void SetPoseParameter( Bytes_t &mdl, int nParameter, float flStart, float flEnd, float flLoop )
{
	if ( flStart == flEnd )
		throw std::runtime_error( "A pose parameter's start and end must differ" );
	Guarded( mdl, [ & ]( CMdl &file ) {
		const auto n = file.Record( 0, POSE_PARAMETERS, POSE_PARAMETER_SIZE, nParameter, "pose parameter" );
		file.PutFloat( n + 8, flStart );
		file.PutFloat( n + 12, flEnd );
		file.PutFloat( n + 16, flLoop );
	} );
}

//-----------------------------------------------------------------------------
// Purpose: The text follows the last solid; it's replaced whole, with its terminator.
//-----------------------------------------------------------------------------
void SetPhysicsText( Bytes_t &phy, const std::string &szText )
{
	if ( szText.find( '\0' ) != std::string::npos )
		throw std::runtime_error( "The physics text can't hold a null character" );
	const CBinary r( phy );
	r.Check( 0, 16 );
	const int nSolids = r.Int( 8 );
	if ( nSolids < 0 || nSolids > 4096 || r.Int( 0 ) < 16 )
		throw std::runtime_error( "Invalid PHY header" );
	std::size_t nAt = static_cast<std::size_t>( r.Int( 0 ) );
	for ( int i = 0; i < nSolids; i++ )
	{
		const int nLength = r.Int( nAt );
		if ( nLength < 0 )
			throw std::runtime_error( "Invalid PHY solid length" );
		r.Check( nAt + 4, static_cast<std::size_t>( nLength ) );
		nAt += static_cast<std::size_t>( nLength ) + 4;
	}
	if ( nAt < phy.size() && r.Text( nAt ) == szText )
		return;
	phy.resize( nAt );
	for ( const char c : szText )
		phy.push_back( static_cast<std::byte>( c ) );
	phy.push_back( std::byte{ 0 } );
}

} // namespace xv::model
