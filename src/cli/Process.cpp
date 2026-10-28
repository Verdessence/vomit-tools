//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Process.h.
//
//===========================================================================//
#include "Process.h"

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace xv::cli
{

#ifdef _WIN32

namespace
{

//-----------------------------------------------------------------------------
// Purpose: One command line from the arguments, each quoted as CreateProcess wants.
//-----------------------------------------------------------------------------
std::string CommandLine( const std::string &szProgram, const std::vector<std::string> &arguments )
{
	std::string szLine;
	auto quote = [ & ]( const std::string &sz ) {
		szLine += '"';
		for ( const char ch : sz )
		{
			if ( ch == '"' )
			{
				szLine += '\\';
			}
			szLine += ch;
		}
		szLine += '"';
	};
	quote( szProgram );
	for ( const std::string &sz : arguments )
	{
		szLine += ' ';
		quote( sz );
	}
	return szLine;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int RunProcess( const std::string &szProgram, const std::vector<std::string> &arguments, const std::string &szDirectory, const std::function<void( std::string_view )> &pfnOutput )
{
	SECURITY_ATTRIBUTES security{ sizeof( SECURITY_ATTRIBUTES ), nullptr, TRUE };
	HANDLE hRead = nullptr, hWrite = nullptr;
	if ( !CreatePipe( &hRead, &hWrite, &security, 0 ) )
	{
		pfnOutput( "could not make a pipe\n" );
		return -1;
	}
	SetHandleInformation( hRead, HANDLE_FLAG_INHERIT, 0 );
	STARTUPINFOA startup{};
	startup.cb = sizeof startup;
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdOutput = hWrite;
	startup.hStdError = hWrite;
	startup.hStdInput = GetStdHandle( STD_INPUT_HANDLE );
	PROCESS_INFORMATION process{};
	std::string szLine = CommandLine( szProgram, arguments );
	const BOOL bOk = CreateProcessA( nullptr, szLine.data(), nullptr, nullptr, TRUE, 0, nullptr, szDirectory.empty() ? nullptr : szDirectory.c_str(), &startup, &process );
	CloseHandle( hWrite );
	if ( !bOk )
	{
		CloseHandle( hRead );
		pfnOutput( "could not start " + szProgram + "\n" );
		return -1;
	}
	char buffer[ 4096 ];
	DWORD nRead = 0;
	while ( ReadFile( hRead, buffer, sizeof buffer, &nRead, nullptr ) && nRead > 0 )
	{
		pfnOutput( std::string_view{ buffer, nRead } );
	}
	CloseHandle( hRead );
	WaitForSingleObject( process.hProcess, INFINITE );
	DWORD nCode = 1;
	GetExitCodeProcess( process.hProcess, &nCode );
	CloseHandle( process.hProcess );
	CloseHandle( process.hThread );
	return static_cast<int>( nCode );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool StartProcess( const std::string &szProgram, const std::vector<std::string> &arguments, const std::string &szDirectory )
{
	STARTUPINFOA startup{};
	startup.cb = sizeof startup;
	PROCESS_INFORMATION process{};
	std::string szLine = CommandLine( szProgram, arguments );
	if ( !CreateProcessA( nullptr, szLine.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, szDirectory.empty() ? nullptr : szDirectory.c_str(), &startup, &process ) )
	{
		return false;
	}
	CloseHandle( process.hProcess );
	CloseHandle( process.hThread );
	return true;
}

#else

namespace
{

//-----------------------------------------------------------------------------
// Purpose: The child's half: into the directory, the pipe (if any) over both outputs, then the
//          program. Only async-signal-safe calls after the fork.
//-----------------------------------------------------------------------------
[[noreturn]] void Exec( const std::string &szProgram, const std::vector<std::string> &arguments, const std::string &szDirectory, int nPipe )
{
	if ( !szDirectory.empty() && chdir( szDirectory.c_str() ) != 0 )
	{
		_exit( 126 );
	}
	if ( nPipe >= 0 )
	{
		dup2( nPipe, STDOUT_FILENO );
		dup2( nPipe, STDERR_FILENO );
		close( nPipe );
	}
	std::vector<char *> argv;
	argv.push_back( const_cast<char *>( szProgram.c_str() ) );
	for ( const std::string &sz : arguments )
	{
		argv.push_back( const_cast<char *>( sz.c_str() ) );
	}
	argv.push_back( nullptr );
	execv( szProgram.c_str(), argv.data() );
	_exit( 127 );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int RunProcess( const std::string &szProgram, const std::vector<std::string> &arguments, const std::string &szDirectory, const std::function<void( std::string_view )> &pfnOutput )
{
	int pipes[ 2 ];
	if ( pipe( pipes ) != 0 )
	{
		pfnOutput( std::string{ "could not make a pipe: " } + std::strerror( errno ) + "\n" );
		return -1;
	}
	const pid_t nChild = fork();
	if ( nChild < 0 )
	{
		pfnOutput( std::string{ "could not start a process: " } + std::strerror( errno ) + "\n" );
		close( pipes[ 0 ] );
		close( pipes[ 1 ] );
		return -1;
	}
	if ( nChild == 0 )
	{
		close( pipes[ 0 ] );
		Exec( szProgram, arguments, szDirectory, pipes[ 1 ] );
	}
	close( pipes[ 1 ] );
	char buffer[ 4096 ];
	for ( ;; )
	{
		const ssize_t nRead = read( pipes[ 0 ], buffer, sizeof buffer );
		if ( nRead <= 0 )
		{
			if ( nRead < 0 && errno == EINTR )
			{
				continue;
			}
			break;
		}
		pfnOutput( std::string_view{ buffer, static_cast<std::size_t>( nRead ) } );
	}
	close( pipes[ 0 ] );
	int nStatus = 0;
	while ( waitpid( nChild, &nStatus, 0 ) < 0 && errno == EINTR )
	{
	}
	if ( WIFEXITED( nStatus ) )
	{
		const int nCode = WEXITSTATUS( nStatus );
		if ( nCode == 127 )
		{
			pfnOutput( "could not start " + szProgram + "\n" );
			return -1;
		}
		return nCode;
	}
	pfnOutput( szProgram + " ended by a signal\n" );
	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: A double fork, so the game is nobody's child to wait for.
//-----------------------------------------------------------------------------
bool StartProcess( const std::string &szProgram, const std::vector<std::string> &arguments, const std::string &szDirectory )
{
	const pid_t nChild = fork();
	if ( nChild < 0 )
	{
		return false;
	}
	if ( nChild == 0 )
	{
		if ( fork() == 0 )
		{
			setsid();
			Exec( szProgram, arguments, szDirectory, -1 );
		}
		_exit( 0 );
	}
	int nStatus = 0;
	waitpid( nChild, &nStatus, 0 );
	return true;
}

#endif

} // namespace xv::cli
