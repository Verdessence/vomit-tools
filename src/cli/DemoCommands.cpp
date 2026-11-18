//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: vomit-tools-cli demo: what a Source demo (.dem) holds, counted (info) or listed in
//          time order (dump): game events, user messages, menus, sounds, temp entities, voice,
//          server prints and the entities' lives; or what it put on screens (feed): chat, the
//          kill feed, HUD messages, menus, hints and rounds, in words.
//
//===========================================================================//
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "demo/Contents.h"
#include "demo/Demo.h"
#include "demo/Feed.h"
#include "demo/Logic.h"
#include "demo/Players.h"
#include "demo/Transcribe.h"
#include "demo/Voice.h"
#include "hud/Hud.h"
#include "kv1/Kv1.h"
#include "audio/Audio.h"
#include "sound/SoundPlayback.h"
#include "vfs/Vfs.h"

int DemoCommand( int argc, char **argv );

using namespace xv::demo;

namespace
{

constexpr const char *DEMO_USAGE = "vomit-tools-cli demo info <file.dem>                 the header and a count of everything in it\n"
								   "vomit-tools-cli demo dump <file.dem> [--only KINDS] [--from TICK] [--to TICK]\n"
								   "                                                       everything decoded, a line each, in time order\n"
								   "vomit-tools-cli demo players <file.dem>              who had each player slot, from when, with Steam ids\n"
								   "vomit-tools-cli demo sounds <file.dem> [--game <folder>]\n"
								   "                                                       every sound it starts: how often, its file, and whether\n"
								   "                                                       that is found (the game's search paths, then the map's\n"
								   "                                                       packed files) and decodes; the game is the demo's folder\n"
								   "vomit-tools-cli demo voice <file.dem> [--wav <folder>] [--transcribe <model.bin>] [--language <code>]\n"
								   "                                                       every stretch of talking decoded (Steam voice, CELT): how long,\n"
								   "                                                       what didn't decode; --wav writes each as a WAV there;\n"
								   "                                                       --transcribe puts each in words with a whisper.cpp model\n"
								   "vomit-tools-cli demo logic <file.dem> <map.bsp>      the map's outputs that may have made what the demo shows\n"
								   "                                                       happen (guesses, the likely first)\n"
								   "vomit-tools-cli demo feed <file.dem>                 what it put on screens, in words (the game's language\n"
								   "                                                       file is read from resource/ beside the demo)\n"
								   "  KINDS: a comma list of events, user, menus, sounds, temp, voice, prints, entities, stats\n"
								   "  (stats: the scoreboard's and HUD's numbers as they change; only when asked for)\n";

//-----------------------------------------------------------------------------
// Purpose: A text with its line breaks and other control characters shown.
//-----------------------------------------------------------------------------
std::string Printable( std::string_view szText )
{
	std::string szOut;
	for ( const char ch : szText )
	{
		if ( ch == '\n' )
		{
			szOut += "\\n";
		}
		else if ( static_cast<unsigned char>( ch ) < 0x20 )
		{
			char szCode[ 8 ];
			std::snprintf( szCode, sizeof( szCode ), "\\x%02x", static_cast<unsigned char>( ch ) );
			szOut += szCode;
		}
		else
		{
			szOut.push_back( ch );
		}
	}
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: A number as short as it can be written.
//-----------------------------------------------------------------------------
std::string Number( double flValue )
{
	char szText[ 32 ];
	std::snprintf( szText, sizeof( szText ), "%g", flValue );
	return szText;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string MenuText( const std::vector<MenuKey_t> &keys )
{
	std::string szText;
	for ( const MenuKey_t &key : keys )
	{
		if ( !szText.empty() )
		{
			szText += " ";
		}
		szText += key.m_szName;
		if ( key.m_Children.empty() )
		{
			szText += "=\"" + Printable( key.m_szValue ) + "\"";
		}
		else
		{
			szText += "{ " + MenuText( key.m_Children ) + " }";
		}
	}
	return szText;
}

struct Line_t
{
	int m_nTick = 0;
	std::string m_szText;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void PrintInfo( const CDemo &demo, const CContents &contents, double flSeconds )
{
	if ( const auto &info = contents.GetServerInfo() )
	{
		std::printf( "map hash: " );
		for ( const std::uint8_t nByte : info->m_MapHash )
			std::printf( "%02x", nByte );
		std::printf( "\nsky:      %s\n", info->m_szSky.c_str() );
	}

	const Header_t &header = demo.GetHeader();
	std::printf( "server:   %s\nrecorder: %s%s\nmap:      %s\ngame:     %s\nlength:   %.1f s, %d ticks, %zu frames\n", header.m_szServer.c_str(), header.m_szClient.c_str(), demo.IsSourceTv() ? " (SourceTV)" : "", header.m_szMap.c_str(), header.m_szGameDir.c_str(), header.m_flSeconds, demo.GetLastTick(), demo.GetFrames().size() );
	std::printf( "read in:  %.2f s\n", flSeconds );
	if ( !contents.GetWarning().empty() )
	{
		std::printf( "warning:  %s\n", contents.GetWarning().c_str() );
	}

	std::printf( "\nmessages:\n" );
	for ( const auto &[ nKind, nCount ] : contents.GetMessageCounts() )
	{
		std::printf( "  %-24s %8d\n", std::string( MessageKindName( nKind ) ).c_str(), nCount );
	}

	const std::string szGame = contents.GetServerInfo() ? contents.GetServerInfo()->m_szGameDir : header.m_szGameDir;
	std::printf( "\nuser messages:\n" );
	for ( const auto &[ nKind, nCount ] : contents.GetUserMessageCounts() )
	{
		const std::string_view szName = UserMessageName( szGame, nKind );
		int nRead = 0;
		for ( const UserMessage_t &message : contents.GetUserMessages() )
		{
			nRead += message.m_nKind == nKind && message.m_bRead;
		}
		std::printf( "  %3d %-20s %8d  (%d decoded)\n", nKind, szName.empty() ? "?" : std::string( szName ).c_str(), nCount, nRead );
	}

	std::map<std::string, int> events;
	for ( const GameEvent_t &event : contents.GetEvents() )
	{
		events[ contents.GetEventKinds()[ static_cast<std::size_t>( event.m_nKind ) ].m_szName ]++;
	}
	std::printf( "\ngame events (%zu kinds in the demo's list):\n", contents.GetEventKinds().size() );
	for ( const auto &[ szName, nCount ] : events )
	{
		std::printf( "  %-32s %8d\n", szName.c_str(), nCount );
	}

	std::map<int, int> menus;
	for ( const Menu_t &menu : contents.GetMenus() )
	{
		menus[ menu.m_nKind ]++;
	}
	std::printf( "\nmenus:\n" );
	for ( const auto &[ nKind, nCount ] : menus )
	{
		std::printf( "  %-24s %8d\n", std::string( MenuKindName( nKind ) ).c_str(), nCount );
	}

	std::printf( "\nsounds: %zu\n", contents.GetSounds().size() );
	std::map<std::string, int> sounds;
	for ( const Sound_t &sound : contents.GetSounds() )
	{
		sounds[ contents.SoundName( sound ) ]++;
	}
	std::vector<std::pair<std::string, int>> topSounds( sounds.begin(), sounds.end() );
	std::ranges::sort( topSounds, []( const auto &a, const auto &b ) { return a.second > b.second; } );
	for ( std::size_t i = 0; i < topSounds.size() && i < 12; i++ )
	{
		std::printf( "  %-56s %8d\n", topSounds[ i ].first.c_str(), topSounds[ i ].second );
	}

	std::map<int, int> temps;
	for ( const TempEntity_t &tempEntity : contents.GetTempEntities() )
	{
		temps[ tempEntity.m_nClass ]++;
	}
	std::printf( "\ntemp entities:\n" );
	for ( const auto &[ nClass, nCount ] : temps )
	{
		std::printf( "  %-32s %8d\n", std::string( contents.ClassName( nClass ) ).c_str(), nCount );
	}

	std::printf( "\nentity messages:\n" );
	for ( const auto &[ nClass, nCount ] : contents.GetEntityMessageCounts() )
	{
		std::printf( "  %-32s %8d\n", std::string( contents.ClassName( nClass ) ).c_str(), nCount );
	}

	std::map<int, std::pair<int, std::size_t>> classes; // lives, keys
	for ( const EntityLife_t &life : contents.GetLives() )
	{
		auto &entry = classes[ life.m_nClass ];
		entry.first++;
		for ( const std::vector<EntityKey_t> &keys : life.m_Fields )
		{
			entry.second += keys.size();
		}
	}
	std::printf( "\nentities: %zu lives, %.1f MB of keys\n", contents.GetLives().size(), static_cast<double>( contents.GetEntityBytes() ) / ( 1024.0 * 1024.0 ) );
	for ( const auto &[ nClass, entry ] : classes )
	{
		std::printf( "  %-32s %6d lives %9zu keys\n", std::string( contents.ClassName( nClass ) ).c_str(), entry.first, entry.second );
	}

	const VoiceCodec_t &codec = contents.GetVoiceCodec();
	std::size_t nVoiceBytes = 0;
	std::map<std::string, int> voiceCodecs;
	int nChecked = 0;
	for ( const VoicePacket_t &packet : contents.GetVoicePackets() )
	{
		nVoiceBytes += packet.m_nBytes;
		if ( codec.m_szCodec == "steam" )
		{
			const auto voice = ParseSteamVoice( contents.GetVoiceBytes( packet ) );
			voiceCodecs[ voice ? std::string( SteamVoiceCodecName( voice->m_nCodec ) ) + " " + std::to_string( voice->m_nSampleRate ) + " Hz" : std::string( "unreadable" ) ]++;
			nChecked += voice && voice->m_bChecked;
		}
	}
	std::printf( "\nvoice: codec \"%s\" (quality %d), %zu packets, %zu bytes, %zu times talking\n", codec.m_szCodec.c_str(), codec.m_nQuality, contents.GetVoicePackets().size(), nVoiceBytes, contents.GetSpeech().size() );
	for ( const auto &[ szCodec, nCount ] : voiceCodecs )
	{
		std::printf( "  %-32s %8d\n", szCodec.c_str(), nCount );
	}
	if ( !voiceCodecs.empty() )
	{
		std::printf( "  CRC matched on %d\n", nChecked );
	}

	std::printf( "\nstring tables:\n" );
	for ( const auto &[ szName, nCount ] : contents.GetStringTables() )
	{
		std::printf( "  %-24s %8d\n", szName.c_str(), nCount );
	}
	std::printf( "\nserver prints: %zu\n", contents.GetPrints().size() );
	std::printf( "misreads: %d\n", contents.GetMisreadCount() );
	for ( const std::string &szMisread : contents.GetMisreads() )
	{
		std::printf( "  %s\n", szMisread.c_str() );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Every decoded thing a line, sorted by tick (stable: the file's order within one).
//-----------------------------------------------------------------------------
void PrintDump( const CContents &contents, const std::set<std::string> &only, int nFrom, int nTo )
{
	const auto wanted = [ & ]( const char *pszKind, int nTick ) { return ( only.empty() || only.contains( pszKind ) ) && nTick >= nFrom && nTick <= nTo; };
	std::vector<Line_t> lines;
	for ( const GameEvent_t &event : contents.GetEvents() )
	{
		if ( !wanted( "events", event.m_nTick ) )
		{
			continue;
		}
		const EventKind_t &kind = contents.GetEventKinds()[ static_cast<std::size_t>( event.m_nKind ) ];
		std::string szText = "event " + kind.m_szName;
		for ( std::size_t i = 0; i < kind.m_Keys.size() && i < event.m_Values.size(); i++ )
		{
			const Value_t &value = event.m_Values[ i ];
			szText += " " + kind.m_Keys[ i ].m_szName + "=" + ( kind.m_Keys[ i ].m_nKind == EventKeyKind_t::EVENT_KEY_KIND_STRING ? "\"" + Printable( value.m_szText ) + "\"" : Number( value.m_flNumber ) );
		}
		lines.push_back( { event.m_nTick, std::move( szText ) } );
	}
	for ( const UserMessage_t &message : contents.GetUserMessages() )
	{
		if ( !wanted( "user", message.m_nTick ) )
		{
			continue;
		}
		std::string szText = "user " + ( message.m_szKind.empty() ? std::to_string( message.m_nKind ) : std::string( message.m_szKind ) );
		if ( message.m_bRead )
		{
			for ( const UserField_t &field : message.m_Fields )
			{
				szText += " " + std::string( field.m_szName ) + "=" + ( field.m_bText ? "\"" + Printable( field.m_Value.m_szText ) + "\"" : Number( field.m_Value.m_flNumber ) );
			}
		}
		else
		{
			szText += " (" + std::to_string( message.m_Data.size() ) + " bytes:";
			for ( std::size_t i = 0; i < message.m_Data.size() && i < 24; i++ )
			{
				char szByte[ 4 ];
				std::snprintf( szByte, sizeof( szByte ), " %02x", static_cast<unsigned>( message.m_Data[ i ] ) );
				szText += szByte;
			}
			szText += message.m_Data.size() > 24 ? " ...)" : ")";
		}
		lines.push_back( { message.m_nTick, std::move( szText ) } );
	}
	for ( const Menu_t &menu : contents.GetMenus() )
	{
		if ( wanted( "menus", menu.m_nTick ) )
		{
			lines.push_back( { menu.m_nTick, "menu " + std::string( MenuKindName( menu.m_nKind ) ) + " " + ( menu.m_bRead ? MenuText( menu.m_Keys ) : std::string( "(unreadable)" ) ) } );
		}
	}
	for ( const Sound_t &sound : contents.GetSounds() )
	{
		if ( !wanted( "sounds", sound.m_nTick ) )
		{
			continue;
		}
		char szText[ 512 ];
		std::snprintf( szText, sizeof( szText ), "sound %s%s entity=%d channel=%d volume=%.2f level=%d pitch=%d at=(%g %g %g)%s", sound.IsStop() ? "STOP " : "", contents.SoundName( sound ).c_str(), sound.m_nEntity, sound.m_nChannel, sound.m_flVolume, sound.m_nSoundLevel, sound.m_nPitch, sound.m_vecOrigin.m_flX, sound.m_vecOrigin.m_flY, sound.m_vecOrigin.m_flZ, sound.m_flDelay != 0.0f ? ( " delay=" + Number( sound.m_flDelay ) ).c_str() : "" );
		lines.push_back( { sound.m_nTick, szText } );
	}
	for ( const TempEntity_t &tempEntity : contents.GetTempEntities() )
	{
		if ( !wanted( "temp", tempEntity.m_nTick ) )
		{
			continue;
		}
		std::string szText = "temp " + std::string( contents.ClassName( tempEntity.m_nClass ) );
		if ( tempEntity.m_flDelay != 0.0f )
		{
			szText += " delay=" + Number( tempEntity.m_flDelay );
		}
		const std::span<const FlatProp_t> props = contents.GetTempProps( tempEntity.m_nClass );
		const std::span<const PropValue_t> values = contents.GetTempValues( tempEntity );
		for ( std::size_t i = 0; i < props.size() && i < values.size(); i++ )
		{
			const FlatProp_t &prop = props[ i ];
			if ( prop.m_nKind == PropKind_t::PROP_KIND_STRING || prop.m_nKind == PropKind_t::PROP_KIND_ARRAY )
			{
				continue;
			}
			szText += " " + prop.m_szName + "=" + Number( values[ i ][ 0 ] );
			if ( prop.m_nKind == PropKind_t::PROP_KIND_VECTOR || prop.m_nKind == PropKind_t::PROP_KIND_VECTOR_XY )
			{
				szText += "," + Number( values[ i ][ 1 ] );
				if ( prop.m_nKind == PropKind_t::PROP_KIND_VECTOR )
				{
					szText += "," + Number( values[ i ][ 2 ] );
				}
			}
		}
		lines.push_back( { tempEntity.m_nTick, std::move( szText ) } );
	}
	for ( const Speech_t &speech : contents.GetSpeech() )
	{
		if ( wanted( "voice", speech.m_nFrom ) )
		{
			lines.push_back( { speech.m_nFrom, "voice entity=" + std::to_string( speech.m_nEntity ) + " talks to tick " + std::to_string( speech.m_nTo ) + " (" + std::to_string( speech.m_nPackets ) + " packets)" } );
		}
	}
	for ( const auto &[ nTick, szPrint ] : contents.GetPrints() )
	{
		if ( wanted( "prints", nTick ) )
		{
			lines.push_back( { nTick, "print \"" + Printable( szPrint ) + "\"" } );
		}
	}
	for ( const auto &[ nTick, szCommand ] : contents.GetServerCommands() )
	{
		if ( wanted( "prints", nTick ) )
		{
			lines.push_back( { nTick, "command \"" + Printable( szCommand ) + "\"" } );
		}
	}
	for ( const EntityLife_t &life : contents.GetLives() )
	{
		const std::string szClass( contents.ClassName( life.m_nClass ) );
		if ( wanted( "entities", life.m_nFrom ) )
		{
			const std::array<float, 3> model = life.ValueAt( ENTITY_FIELD_MODEL, life.m_nFrom );
			const std::array<float, 3> origin = life.ValueAt( ENTITY_FIELD_ORIGIN, life.m_nFrom );
			lines.push_back( { life.m_nFrom, "entity " + std::to_string( life.m_nEntity ) + " " + szClass + " comes, model " + std::string( contents.ModelName( static_cast<int>( model[ 0 ] ) ) ) + " at (" + Number( origin[ 0 ] ) + " " + Number( origin[ 1 ] ) + " " + Number( origin[ 2 ] ) + ")" } );
		}
		if ( life.m_nTo >= 0 && wanted( "entities", life.m_nTo ) )
		{
			lines.push_back( { life.m_nTo, "entity " + std::to_string( life.m_nEntity ) + " " + szClass + " goes" } );
		}
		// Stats are many (pings change all the time): only when named.
		const std::span<const std::string> stats = contents.GetStats( life.m_nClass );
		for ( std::size_t nStat = 0; only.contains( "stats" ) && nStat < life.m_Stats.size() && nStat < stats.size(); nStat++ )
		{
			for ( const StatKey_t &key : life.m_Stats[ nStat ] )
			{
				if ( key.m_nTick >= nFrom && key.m_nTick <= nTo )
				{
					lines.push_back( { key.m_nTick, "stat " + std::to_string( life.m_nEntity ) + " " + szClass + " " + stats[ nStat ] + "=" + Number( key.m_flValue ) } );
				}
			}
		}
	}
	if ( only.contains( "stats" ) )
	{
		lines.push_back( { nFrom, "server time at tick " + std::to_string( nFrom ) + ": " + Number( static_cast<float>( contents.ServerTimeAt( nFrom ) ) ) + " s" } );
	}
	std::ranges::stable_sort( lines, {}, &Line_t::m_nTick );
	const double flInterval = contents.GetTickInterval();
	for ( const Line_t &line : lines )
	{
		std::printf( "%7d %8.2f  %s\n", line.m_nTick, line.m_nTick * flInterval, line.m_szText.c_str() );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The language file beside the demo (a game folder holds both), as tokens.
//-----------------------------------------------------------------------------
std::map<std::string, std::string> ReadLanguage( const std::filesystem::path &demoFile, const std::string &szGame )
{
	std::map<std::string, std::string> tokens;
	for ( const std::string &szName : { std::string( "valve_english.txt" ), szGame + "_english.txt" } )
	{
		std::ifstream in( demoFile.parent_path() / "resource" / szName, std::ios::binary );
		if ( !in )
			continue;
		std::string szText( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
		for ( auto &[ szToken, szValue ] : xv::hud::ReadTokens( xv::kv1::Parse( xv::hud::LanguageText( szText ) ).m_Document ) )
			tokens[ szToken ] = std::move( szValue );
	}
	return tokens;
}

//-----------------------------------------------------------------------------
// Purpose: Every sound the demo starts, by name: how many starts and stops, when first, the
//          flags, channels and levels seen, and whether the file is found and decodes; then
//          the totals. For finding why a sound isn't heard in the viewer.
//-----------------------------------------------------------------------------
int PrintSounds( const CDemo &demo, const CContents &contents, const std::string &szGame )
{
	std::string szError;
	const auto game = xv::vfs::CGameInfo::Load( std::filesystem::path( szGame ) / "gameinfo.txt", &szError );
	if ( !game )
	{
		std::fprintf( stderr, "demo sounds: %s\n", szError.c_str() );
		return 1;
	}
	xv::vfs::CFileSystem files;
	files.Mount( *game );
	const std::string szMap = demo.GetHeader().m_szMap;
	const xv::vfs::CMount *pMapMount = nullptr;
	for ( const char *pszFolder : { "maps", "download/maps" } )
	{
		const std::filesystem::path map = std::filesystem::path( szGame ) / pszFolder / ( szMap + ".bsp" );
		if ( std::filesystem::exists( map ) )
		{
			pMapMount = files.AddPackage( map, { "game" } );
			std::printf( "map: %s\n", map.string().c_str() );
			break;
		}
	}
	struct Use_t
	{
		int m_nStarts = 0;
		int m_nStops = 0;
		int m_nFirst = -1;
		std::set<int> m_Channels;
		std::set<int> m_Levels;
		int m_nFlags = 0;
		int m_nEverywhere = 0;
		bool m_bSentence = false;
	};
	std::map<std::string, Use_t> uses;
	for ( const Sound_t &sound : contents.GetSounds() )
	{
		Use_t &use = uses[ contents.SoundName( sound ) ];
		use.m_bSentence = sound.m_bSentence;
		if ( sound.IsStop() )
		{
			use.m_nStops++;
			continue;
		}
		use.m_nStarts++;
		use.m_nFirst = use.m_nFirst < 0 ? sound.m_nTick : use.m_nFirst;
		use.m_Channels.insert( sound.m_nChannel );
		use.m_Levels.insert( sound.m_nSoundLevel );
		use.m_nFlags |= sound.m_nFlags;
		use.m_nEverywhere += ( sound.m_nSoundLevel & 0xff ) == 0 ? 1 : 0;
	}
	int nMissing = 0;
	int nUndecoded = 0;
	int nMissingStarts = 0;
	int nStarts = 0;
	for ( const auto &[ szName, use ] : uses )
	{
		nStarts += use.m_nStarts;
		std::string szWhere;
		if ( use.m_bSentence )
		{
			szWhere = "sentence";
		}
		else if ( const std::string szPath = xv::sound::WaveGamePath( szName ); szPath.empty() )
		{
			szWhere = "no file (" + szName + ")";
		}
		else if ( const xv::vfs::CMount *pMount = files.Resolve( szPath ); !pMount )
		{
			szWhere = "MISSING " + szPath;
			nMissing++;
			nMissingStarts += use.m_nStarts;
		}
		else
		{
			std::string szDecode;
			const auto bytes = pMount->Read( xv::vfs::NormalizePath( szPath ) );
			const auto pClip = bytes ? xv::audio::Decode( *bytes, &szDecode ) : nullptr;
			const std::string szFrom = pMount == pMapMount ? "map" : pMount->GetLocation().filename().string();
			if ( !pClip )
			{
				szWhere = "UNDECODED (" + szDecode + ") from " + szFrom;
				nUndecoded++;
				nMissingStarts += use.m_nStarts;
			}
			else
			{
				char szClip[ 96 ];
				std::snprintf( szClip, sizeof( szClip ), "%.1f s %u Hz%s, from ", pClip->GetSeconds(), pClip->m_nSampleRate, pClip->m_LoopStart ? " loops" : "" );
				szWhere = szClip + szFrom;
			}
		}
		std::string szChannels;
		for ( const int nChannel : use.m_Channels )
			szChannels += ( szChannels.empty() ? "" : "," ) + std::to_string( nChannel );
		std::string szLevels;
		for ( const int nLevel : use.m_Levels )
			szLevels += ( szLevels.empty() ? "" : "," ) + std::to_string( nLevel & 0xff );
		std::printf( "%5d %4d %7d  ch %-6s lvl %-10s flags %03x%s  %s  %s\n", use.m_nStarts, use.m_nStops, use.m_nFirst, szChannels.c_str(), szLevels.c_str(), use.m_nFlags, use.m_nEverywhere ? " everywhere" : "", Printable( szName ).c_str(), szWhere.c_str() );
	}
	std::printf( "%zu sounds, %d starts; %d not found and %d not decoded (%d starts lost)\n", uses.size(), nStarts, nMissing, nUndecoded, nMissingStarts );
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: Every speech decoded: its talker, ticks and seconds of sound, and frames that
//          didn't decode; with a folder, each written there as 16-bit WAV; with a model, what
//          was said.
//-----------------------------------------------------------------------------
int PrintVoice( const CContents &contents, const std::string &szFolder, const std::string &szModel, const std::string &szLanguage )
{
	std::unique_ptr<CTranscriber> pTranscriber;
	if ( !szModel.empty() )
	{
		pTranscriber = std::make_unique<CTranscriber>( szModel, szLanguage );
		if ( !pTranscriber->IsOk() )
		{
			std::fprintf( stderr, "%s\n", pTranscriber->GetError().c_str() );
			return 1;
		}
	}
	std::map<std::size_t, Transcript_t> transcripts;
	if ( pTranscriber )
	{
		std::vector<std::size_t> order( contents.GetSpeech().size() );
		for ( std::size_t i = 0; i < order.size(); i++ )
			order[ i ] = i;
		TranscribeSpeech( contents, *pTranscriber, order, [ & ]( std::size_t i, const Transcript_t &transcript ) { transcripts[ i ] = transcript; } );
	}
	const VoiceCodec_t &codec = contents.GetVoiceCodec();
	if ( !CVoiceDecoder( codec ).IsSupported() )
	{
		std::printf( "voice codec \"%s\" isn't decoded\n", codec.m_szCodec.c_str() );
		return 0;
	}
	if ( !szFolder.empty() )
	{
		std::filesystem::create_directories( szFolder );
	}
	double flTotal = 0.0;
	int nFailures = 0;
	int nIndex = 0;
	for ( const Speech_t &speech : contents.GetSpeech() )
	{
		int nRate = 0;
		int nFailed = 0;
		const std::vector<float> samples = DecodeSpeech( contents, speech, &nRate, &nFailed );
		const double flSeconds = nRate ? static_cast<double>( samples.size() ) / nRate : 0.0;
		const double flSpan = ( speech.m_nTo - speech.m_nFrom ) * contents.GetTickInterval();
		flTotal += flSeconds;
		nFailures += nFailed;
		std::printf( "%7d %8.2f  entity %2d  %4d packets  %6.2f s of sound over %6.2f s%s\n", speech.m_nFrom, speech.m_nFrom * contents.GetTickInterval(), speech.m_nEntity, speech.m_nPackets, flSeconds, flSpan, nFailed ? ( "  " + std::to_string( nFailed ) + " failed" ).c_str() : "" );
		if ( const auto said = transcripts.find( static_cast<std::size_t>( nIndex ) ); said != transcripts.end() )
		{
			const Transcript_t &transcript = said->second;
			std::printf( "                 %s%s%s\n", transcript.m_szLanguage.empty() ? "" : ( "[" + transcript.m_szLanguage + "] " ).c_str(), transcript.m_szText.c_str(), transcript.m_szText.empty() ? "(nothing heard)" : "" );
		}
		if ( !szFolder.empty() && !samples.empty() )
		{
			char szName[ 64 ];
			std::snprintf( szName, sizeof( szName ), "%04d-tick%d-entity%d.wav", nIndex, speech.m_nFrom, speech.m_nEntity );
			std::ofstream out( std::filesystem::path( szFolder ) / szName, std::ios::binary );
			const std::vector<std::byte> wav = xv::audio::EncodeWav( samples, static_cast<std::uint32_t>( nRate ) );
			out.write( reinterpret_cast<const char *>( wav.data() ), static_cast<std::streamsize>( wav.size() ) );
		}
		nIndex++;
	}
	std::printf( "%zu stretches, %.1f s of sound; %d frames didn't decode\n", contents.GetSpeech().size(), flTotal, nFailures );
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: Each slot's players in turn: from which tick, the name, the game's Steam id, the
//          64-bit one and the profile, bots and SourceTV said as such.
//-----------------------------------------------------------------------------
void PrintPlayers( const CPlayers &players, double flTickInterval )
{
	for ( const PlayerTrack_t &track : players.GetTracks() )
	{
		for ( const PlayerIdentity_t &identity : track.m_Identities )
		{
			std::string szKind;
			if ( identity.m_bSourceTv )
			{
				szKind = "SourceTV";
			}
			else if ( identity.m_bBot )
			{
				szKind = "bot";
			}
			else if ( const auto nId64 = SteamId64( identity ) )
			{
				szKind = std::to_string( *nId64 ) + "  " + SteamProfileUrl( *nId64 );
			}
			std::printf( "%3d %7d %8.2f  %-32s %-20s %s\n", track.m_nEntity, identity.m_nTick, identity.m_nTick * flTickInterval, identity.m_szName.empty() ? "(left)" : Printable( identity.m_szName ).c_str(), identity.m_szSteamId.c_str(), szKind.c_str() );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: The feed's lists merged in time order, colour codes left out.
//-----------------------------------------------------------------------------
void PrintFeed( const CDemo &demo, const CContents &contents, const CPlayers &players, const std::filesystem::path &file )
{
	const std::string szGame = contents.GetServerInfo() ? contents.GetServerInfo()->m_szGameDir : std::string( "cstrike" );
	const std::map<std::string, std::string> tokens = ReadLanguage( file, szGame );
	const Localize_t localize = [ & ]( std::string_view szToken ) -> std::optional<std::string> {
		std::string szLower( szToken );
		std::ranges::transform( szLower, szLower.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
		const auto found = tokens.find( szLower );
		return found == tokens.end() ? std::nullopt : std::optional<std::string>( found->second );
	};
	const CFeed feed = CFeed::Build( contents, players, localize, demo.GetCommands() );
	const auto nameOf = [ & ]( int nEntity, int nTick ) -> std::string {
		if ( const PlayerTrack_t *pTrack = players.FindTrack( nEntity ) )
		{
			if ( const PlayerIdentity_t *pIdentity = pTrack->IdentityAt( nTick ) )
				return pIdentity->m_szName;
		}
		return nEntity ? "#" + std::to_string( nEntity ) : std::string( "world" );
	};
	std::vector<Line_t> lines;
	static const char *const KIND_NAMES[] = { "chat", "radio", "notice", "console", "center", "hint", "keyhint", "dialog" };
	for ( const FeedLine_t &line : feed.GetLines() )
		lines.push_back( { line.m_nTick, std::string( KIND_NAMES[ static_cast<int>( line.m_nKind ) ] ) + " \"" + Printable( StripColors( line.m_szText ) ) + "\"" } );
	for ( const Kill_t &kill : feed.GetKills() )
		lines.push_back( { kill.m_nTick, "kill " + nameOf( kill.m_nAttacker, kill.m_nTick ) + " [" + kill.m_szWeapon + ( kill.m_bHeadshot ? ", headshot" : "" ) + "] " + nameOf( kill.m_nVictim, kill.m_nTick ) } );
	for ( const HudText_t &text : feed.GetHudTexts() )
		lines.push_back( { text.m_nTick, "hud channel " + std::to_string( text.m_nChannel ) + " for " + Number( static_cast<float>( text.GetDuration() ) ) + " s: \"" + Printable( text.m_szText ) + "\"" } );
	for ( const MenuShown_t &menu : feed.GetMenus() )
		lines.push_back( { menu.m_nTick, "menu" + std::string( menu.m_nPicked >= 0 ? " (picked at " + std::to_string( menu.m_nPicked ) + ")" : "" ) + ": \"" + Printable( menu.m_szText ) + "\"" } );
	for ( const PanelShown_t &panel : feed.GetPanels() )
		lines.push_back( { panel.m_nTick, "panel " + panel.m_szName + ( panel.m_bShow ? " shown" : " closed" ) } );
	for ( const Fade_t &fade : feed.GetFades() )
		lines.push_back( { fade.m_nTick, "fade flags " + std::to_string( fade.m_nFlags ) + " over " + Number( static_cast<float>( fade.m_flDuration ) ) + " s, held " + Number( static_cast<float>( fade.m_flHold ) ) + " s" } );
	for ( const Round_t &round : feed.GetRounds() )
	{
		lines.push_back( { round.m_nStart, "round starts" } );
		if ( round.m_nEnd >= 0 )
			lines.push_back( { round.m_nEnd, "round ends: " + round.m_szMessage } );
	}
	for ( const Speech_t &speech : contents.GetSpeech() )
		lines.push_back( { speech.m_nFrom, "talks " + nameOf( speech.m_nEntity, speech.m_nFrom ) + " for " + Number( static_cast<float>( ( speech.m_nTo - speech.m_nFrom ) * contents.GetTickInterval() ) ) + " s" } );
	std::ranges::stable_sort( lines, {}, &Line_t::m_nTick );
	for ( const Line_t &line : lines )
		std::printf( "%7d %8.2f  %s\n", line.m_nTick, line.m_nTick * contents.GetTickInterval(), line.m_szText.c_str() );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int DemoCommand( int argc, char **argv )
{
	if ( argc < 2 || std::string_view( argv[ 0 ] ) == "--help" )
	{
		std::printf( "%s", DEMO_USAGE );
		return argc < 1 ? 1 : 0;
	}
	const std::string_view szCommand = argv[ 0 ];
	std::ifstream in( argv[ 1 ], std::ios::binary );
	if ( !in )
	{
		std::fprintf( stderr, "can't open %s\n", argv[ 1 ] );
		return 1;
	}
	std::vector<char> chars( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
	const std::span<const std::byte> bytes( reinterpret_cast<const std::byte *>( chars.data() ), chars.size() );
	std::string szError;
	const auto demo = CDemo::Parse( bytes, &szError );
	if ( !demo )
	{
		std::fprintf( stderr, "%s: %s\n", argv[ 1 ], szError.c_str() );
		return 1;
	}
	const auto start = std::chrono::steady_clock::now();
	const CContents contents = CContents::Read( *demo, bytes );
	const double flSeconds = std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
	if ( szCommand == "info" )
	{
		PrintInfo( *demo, contents, flSeconds );
		return 0;
	}
	if ( szCommand == "logic" )
	{
		if ( argc < 3 )
		{
			std::printf( "%s", DEMO_USAGE );
			return 1;
		}
		std::ifstream map( argv[ 2 ], std::ios::binary );
		const std::vector<char> mapChars( ( std::istreambuf_iterator<char>( map ) ), std::istreambuf_iterator<char>() );
		const LogicMap_t logicMap = ReadLogicMap( { reinterpret_cast<const std::byte *>( mapChars.data() ), mapChars.size() } );
		if ( logicMap.m_Entities.empty() )
		{
			std::fprintf( stderr, "%s: not a map with entities\n", argv[ 2 ] );
			return 1;
		}
		const CPlayers players = CPlayers::Read( *demo, bytes );
		const CFeed feed = CFeed::Build( contents, players, {}, demo->GetCommands() );
		const auto logicStart = std::chrono::steady_clock::now();
		const std::vector<LogicGuess_t> guesses = GuessLogic( contents, players, feed, logicMap );
		static const char *const TRUST[] = { "likely", "possible", "unlikely" };
		int nLikely = 0;
		for ( const LogicGuess_t &guess : guesses )
		{
			nLikely += guess.IsLikely() ? 1 : 0;
			std::printf( "%7d %8.2f  %s %s %s\n", guess.m_nTick, guess.m_nTick * contents.GetTickInterval(), guess.m_szClass.c_str(), guess.m_szName.c_str(), std::string( LogicChangeName( guess.m_nChange ) ).c_str() );
			for ( const LogicCause_t &cause : guess.m_Causes )
				std::printf( "                    %-8s %s %s -> %s%s after %.2f s%s%s\n", TRUST[ static_cast<int>( cause.m_nTrust ) ], cause.m_szSource.c_str(), cause.m_szOutput.c_str(), cause.m_szInput.c_str(), cause.m_szParameter.empty() ? "" : ( " (" + cause.m_szParameter + ")" ).c_str(), cause.m_flDelay, cause.m_szWhy.empty() ? "" : ": ", cause.m_szWhy.c_str() );
		}
		std::printf( "%zu changes guessed at, %d likely, in %.0f ms\n", guesses.size(), nLikely, std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - logicStart ).count() );
		return 0;
	}
	if ( szCommand == "sounds" )
	{
		std::string szGame = argc >= 4 && std::string_view( argv[ 2 ] ) == "--game" ? argv[ 3 ] : std::filesystem::path( argv[ 1 ] ).parent_path().string();
		return PrintSounds( *demo, contents, szGame );
	}
	if ( szCommand == "voice" )
	{
		std::string szWav, szModel, szLanguage = "auto";
		for ( int i = 2; i + 1 < argc; i += 2 )
		{
			const std::string_view szOption = argv[ i ];
			( szOption == "--wav" ? szWav : szOption == "--transcribe" ? szModel
																	   : szLanguage ) = argv[ i + 1 ];
		}
		return PrintVoice( contents, szWav, szModel, szLanguage );
	}
	if ( szCommand == "players" )
	{
		PrintPlayers( CPlayers::Read( *demo, bytes ), contents.GetTickInterval() );
		return 0;
	}
	if ( szCommand == "feed" )
	{
		PrintFeed( *demo, contents, CPlayers::Read( *demo, bytes ), argv[ 1 ] );
		return 0;
	}
	if ( szCommand == "dump" )
	{
		std::set<std::string> only;
		int nFrom = 0;
		int nTo = 1 << 30;
		for ( int i = 2; i + 1 < argc; i += 2 )
		{
			const std::string_view szOption = argv[ i ];
			if ( szOption == "--only" )
			{
				std::string szList = argv[ i + 1 ];
				for ( std::size_t nAt = 0; nAt <= szList.size(); )
				{
					const std::size_t nComma = std::min( szList.find( ',', nAt ), szList.size() );
					only.insert( szList.substr( nAt, nComma - nAt ) );
					nAt = nComma + 1;
				}
			}
			else if ( szOption == "--from" )
			{
				nFrom = std::atoi( argv[ i + 1 ] );
			}
			else if ( szOption == "--to" )
			{
				nTo = std::atoi( argv[ i + 1 ] );
			}
		}
		PrintDump( contents, only, nFrom, nTo );
		return 0;
	}
	std::printf( "%s", DEMO_USAGE );
	return 1;
}
