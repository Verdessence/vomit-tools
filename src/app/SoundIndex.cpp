//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SoundIndex.h.
//
//===========================================================================//
#include "SoundIndex.h"

#include <algorithm>
#include <set>

#include "SoundDocument.h"
#include "SoundPlayer.h"
#include "kv1/Kv1.h"
#include "sound/Wav.h"
#include "vfs/Vfs.h"

using xv::sound::FileKind_t;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundIndex::Rebuild( const xv::vfs::CFileSystem &fileSystem )
{
	m_pFileSystem = &fileSystem;
	m_SoundFiles.clear();
	m_Soundscapes.clear();
	m_Sounds.clear();
	m_SoundscapeDocuments.clear();
	m_Loops.clear();

	for ( const std::string &szPath : fileSystem.Find( "sound", "", true ) )
	{
		if ( szPath.ends_with( ".wav" ) || szPath.ends_with( ".mp3" ) )
		{
			m_SoundFiles << QString::fromStdString( szPath.substr( 6 ) );
		}
	}

	for ( const std::string &szFile : fileSystem.Find( "scripts", ".txt" ) )
	{
		const FileKind_t nKind = xv::sound::FileKindForPath( szFile );
		if ( nKind == FileKind_t::FILE_KIND_UNKNOWN )
		{
			continue;
		}
		const auto text = fileSystem.ReadText( szFile );
		if ( !text )
		{
			continue;
		}
		auto parsed = xv::kv1::Parse( *text );
		auto &entries = ( nKind == FileKind_t::FILE_KIND_SOUNDSCAPE ? m_Soundscapes : m_Sounds )[ szFile ];
		for ( const xv::kv1::CNode &root : parsed.m_Document.m_Roots )
		{
			if ( root.IsBlock() )
			{
				entries.push_back( { root.GetKey(), nKind == FileKind_t::FILE_KIND_SOUNDSCAPE ? xv::sound::NestedSoundscapes( root ) : std::vector<std::string>{} } );
			}
		}
		if ( nKind == FileKind_t::FILE_KIND_SOUNDSCAPE )
		{
			m_SoundscapeDocuments.emplace( szFile, std::move( parsed.m_Document ) );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QStringList CSoundIndex::SoundscapeNames( const CSoundDocument *pIncluding ) const
{
	std::set<QString> names;
	const std::string szSkip = pIncluding ? pIncluding->GetGamePath().toStdString() : std::string{};
	for ( const auto &[ file, entries ] : m_Soundscapes )
	{
		if ( file != szSkip )
		{
			for ( const Entry_t &entry : entries )
			{
				names.insert( QString::fromStdString( entry.m_szName ) );
			}
		}
	}
	if ( pIncluding && pIncluding->GetKind() == FileKind_t::FILE_KIND_SOUNDSCAPE )
	{
		for ( const xv::kv1::CNode &root : pIncluding->GetDocument().m_Roots )
		{
			if ( root.IsBlock() )
			{
				names.insert( QString::fromStdString( root.GetKey() ) );
			}
		}
	}
	return { names.begin(), names.end() };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CSoundIndex::HasGame() const
{
	return m_pFileSystem && !m_pFileSystem->GetRecords().empty();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CSoundIndex::Exists( std::string_view szGamePath ) const
{
	return m_pFileSystem && m_pFileSystem->Resolve( szGamePath ) != nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: A soundscape by name, looked up in `first` before the game's other soundscape files.
//-----------------------------------------------------------------------------
const xv::kv1::CNode *CSoundIndex::FindSoundscape( std::string_view szName, const CSoundDocument *pFirst ) const
{
	std::string szSkip;
	if ( pFirst && pFirst->GetKind() == FileKind_t::FILE_KIND_SOUNDSCAPE )
	{
		if ( const xv::kv1::CNode *pFound = pFirst->GetDocument().Find( szName ); pFound && pFound->IsBlock() )
		{
			return pFound;
		}
		szSkip = xv::vfs::NormalizePath( pFirst->GetGamePath().toStdString() );
	}
	for ( const auto &[ file, document ] : m_SoundscapeDocuments )
	{
		if ( file != szSkip )
		{
			if ( const xv::kv1::CNode *pFound = document.Find( szName ); pFound && pFound->IsBlock() )
			{
				return pFound;
			}
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<bool> CSoundIndex::HasLoopPoints( std::string_view szGamePath ) const
{
	auto [ it, inserted ] = m_Loops.try_emplace( std::string{ szGamePath } );
	if ( inserted && m_pFileSystem )
	{
		if ( const auto bytes = m_pFileSystem->Read( szGamePath ) )
		{
			if ( const auto info = xv::sound::ReadWavInfo( *bytes ) )
			{
				it->second = info->m_LoopStart.has_value();
			}
		}
	}
	return it->second;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::string> CSoundIndex::ManifestFiles( FileKind_t nKind ) const
{
	if ( !m_pFileSystem )
	{
		return {};
	}
	const auto text = m_pFileSystem->ReadText( xv::sound::ManifestPathFor( nKind ) );
	return text ? xv::sound::ManifestFiles( xv::kv1::Parse( *text ).m_Document ) : std::vector<std::string>{};
}

//-----------------------------------------------------------------------------
// Purpose: Everything the linter needs, with the document's own file left out of the cross-file
//          maps.
//-----------------------------------------------------------------------------
xv::sound::LintContext_t CSoundIndex::LintContext( const CSoundDocument &document ) const
{
	xv::sound::LintContext_t context;
	// Sounds are looked for in the mounted game and, for files opened from a mod folder, beside them.
	const std::filesystem::path root = document.GetLocalRoot();
	if ( HasGame() || !root.empty() )
	{
		context.m_pfnFileExists = [ this, root ]( std::string_view szPath ) {
			std::error_code ec;
			return Exists( szPath ) || ( !root.empty() && std::filesystem::exists( xv::vfs::FindCaseInsensitive( root / xv::vfs::FromUtf8( szPath ) ), ec ) );
		};
		context.m_pfnHasLoopPoints = [ this, root ]( std::string_view szPath ) -> std::optional<bool> {
			if ( Exists( szPath ) )
			{
				return HasLoopPoints( szPath );
			}
			if ( root.empty() || !m_pFileSystem )
			{
				return std::nullopt;
			}
			const auto bytes = CSoundPlayer::ReadGameFile( *m_pFileSystem, std::string{ szPath }, root );
			const auto info = bytes ? xv::sound::ReadWavInfo( *bytes ) : std::nullopt;
			return info ? std::optional{ info->m_LoopStart.has_value() } : std::nullopt;
		};
	}

	const std::string szSelf = xv::vfs::NormalizePath( document.GetGamePath().toStdString() );
	const bool bSoundscape = document.GetKind() == FileKind_t::FILE_KIND_SOUNDSCAPE;
	for ( const auto &[ file, entries ] : bSoundscape ? m_Soundscapes : m_Sounds )
	{
		if ( file == szSelf )
		{
			continue;
		}
		for ( const Entry_t &entry : entries )
		{
			const std::string szName = xv::sound::ToLower( entry.m_szName );
			if ( bSoundscape )
			{
				context.m_OtherSoundscapes.emplace( szName, file );
				context.m_OtherNesting.emplace( szName, entry.m_Nested );
			}
			else
			{
				context.m_OtherSounds.emplace( szName, file );
			}
		}
	}
	if ( HasGame() )
	{
		const auto listed = ManifestFiles( document.GetKind() );
		context.m_ListedInManifest = std::ranges::find( listed, szSelf ) != listed.end();
	}
	return context;
}
