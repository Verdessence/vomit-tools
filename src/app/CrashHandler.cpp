//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See CrashHandler.h.
//
//===========================================================================//
#include "CrashHandler.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <typeinfo>
#include <vector>

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "DebugLog.h"

#if defined( _WIN32 )
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#if __has_include( <execinfo.h> )
#include <execinfo.h>
#define VOMIT_TOOLS_BACKTRACE 1
#endif
#endif

namespace
{

// Ready before a crash: the handlers mustn't allocate or call into Qt.
char g_szFolder[ 4096 ] = {};
char g_szVersion[ 128 ] = {};
char g_szContext[ 8192 ] = {};
char g_szReason[ 1024 ] = {}; // an uncaught exception's, for the abort that follows it
std::atomic<bool> g_bCrashing{ false };
constexpr char SHOWN_SETTING[] = "crash/shownUntil";
constexpr int DEBUG_LOG_LINES = 60;

//-----------------------------------------------------------------------------
// Purpose: Copies text into a fixed buffer, cut short to fit.
//-----------------------------------------------------------------------------
template <std::size_t N>
void Keep( char ( &szTo )[ N ], const QByteArray &bytes )
{
	const std::size_t n = std::min<std::size_t>( static_cast<std::size_t>( bytes.size() ), N - 1 );
	std::memcpy( szTo, bytes.constData(), n );
	szTo[ n ] = '\0';
}

#if !defined( _WIN32 )

//-----------------------------------------------------------------------------
// Purpose: Writes all of a string, as signal handlers may.
//-----------------------------------------------------------------------------
void Put( int nFile, const char *psz )
{
	std::size_t nLeft = std::strlen( psz );
	while ( nLeft > 0 )
	{
		const ssize_t nWritten = write( nFile, psz, nLeft );
		if ( nWritten <= 0 )
			return;
		psz += nWritten;
		nLeft -= static_cast<std::size_t>( nWritten );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A number as decimal (or hex) text, without printf.
//-----------------------------------------------------------------------------
void PutNumber( int nFile, unsigned long long n, unsigned nBase = 10 )
{
	char sz[ 32 ];
	int i = 31;
	sz[ i ] = '\0';
	do
	{
		sz[ --i ] = "0123456789abcdef"[ n % nBase ];
		n /= nBase;
	}
	while ( n > 0 && i > 0 );
	Put( nFile, sz + i );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const char *SignalName( int nSignal )
{
	switch ( nSignal )
	{
		case SIGSEGV: return "a bad memory access (SIGSEGV)";
		case SIGBUS: return "a bad memory access (SIGBUS)";
		case SIGABRT: return "an abort (SIGABRT)";
		case SIGFPE: return "an arithmetic error (SIGFPE)";
		case SIGILL: return "an illegal instruction (SIGILL)";
		default: return "a signal";
	}
}

//-----------------------------------------------------------------------------
// Purpose: The log, written with system calls alone; then the signal as it would have gone.
//-----------------------------------------------------------------------------
void OnSignal( int nSignal, siginfo_t *pInfo, void * )
{
	if ( !g_bCrashing.exchange( true ) )
	{
		char szPath[ 4200 ];
		const auto nNow = static_cast<unsigned long long>( std::time( nullptr ) );
		std::size_t n = std::strlen( g_szFolder );
		std::memcpy( szPath, g_szFolder, n );
		const char szName[] = "/crash-";
		std::memcpy( szPath + n, szName, sizeof( szName ) - 1 );
		n += sizeof( szName ) - 1;
		char szTime[ 32 ];
		int i = 31;
		szTime[ i ] = '\0';
		unsigned long long nTime = nNow;
		do
		{
			szTime[ --i ] = static_cast<char>( '0' + nTime % 10 );
			nTime /= 10;
		}
		while ( nTime > 0 && i > 0 );
		std::memcpy( szPath + n, szTime + i, std::strlen( szTime + i ) );
		n += std::strlen( szTime + i );
		std::memcpy( szPath + n, ".txt", 5 );
		const int nFile = open( szPath, O_CREAT | O_WRONLY | O_TRUNC, 0644 );
		if ( nFile >= 0 )
		{
			Put( nFile, "vomit-tools " );
			Put( nFile, g_szVersion );
			Put( nFile, " crashed: " );
			Put( nFile, SignalName( nSignal ) );
			if ( pInfo && ( nSignal == SIGSEGV || nSignal == SIGBUS ) )
			{
				Put( nFile, " at 0x" );
				PutNumber( nFile, reinterpret_cast<unsigned long long>( pInfo->si_addr ), 16 );
			}
			Put( nFile, "\n" );
			if ( g_szReason[ 0 ] )
			{
				Put( nFile, g_szReason );
				Put( nFile, "\n" );
			}
			Put( nFile, "\nOpen:\n" );
			Put( nFile, g_szContext[ 0 ] ? g_szContext : "(nothing)\n" );
#if defined( VOMIT_TOOLS_BACKTRACE )
			Put( nFile, "\nStack:\n" );
			void *frames[ 64 ];
			const int nFrames = backtrace( frames, 64 );
			backtrace_symbols_fd( frames, nFrames, nFile );
#endif
			close( nFile );
		}
		Put( STDERR_FILENO, "vomit-tools crashed; log at " );
		Put( STDERR_FILENO, szPath );
		Put( STDERR_FILENO, "\n" );
	}
	signal( nSignal, SIG_DFL );
	raise( nSignal );
}

#else

//-----------------------------------------------------------------------------
// Purpose: The log, written with the system's calls alone; the stack as return addresses
//          with the module each is in.
//-----------------------------------------------------------------------------
LONG WINAPI OnException( EXCEPTION_POINTERS *pException )
{
	if ( g_bCrashing.exchange( true ) )
		return EXCEPTION_CONTINUE_SEARCH;
	char szPath[ 4200 ];
	std::snprintf( szPath, sizeof( szPath ), "%s\\crash-%llu.txt", g_szFolder, static_cast<unsigned long long>( std::time( nullptr ) ) );
	const HANDLE hFile = CreateFileA( szPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
	if ( hFile == INVALID_HANDLE_VALUE )
		return EXCEPTION_CONTINUE_SEARCH;
	const auto put = [ hFile ]( const char *psz ) {
		DWORD nWritten = 0;
		WriteFile( hFile, psz, static_cast<DWORD>( std::strlen( psz ) ), &nWritten, nullptr );
	};
	char szLine[ 512 ];
	const EXCEPTION_RECORD *pRecord = pException ? pException->ExceptionRecord : nullptr;
	std::snprintf( szLine, sizeof( szLine ), "vomit-tools %s crashed: exception 0x%08lx at %p\n", g_szVersion, pRecord ? static_cast<unsigned long>( pRecord->ExceptionCode ) : 0ul, pRecord ? pRecord->ExceptionAddress : nullptr );
	put( szLine );
	if ( g_szReason[ 0 ] )
	{
		put( g_szReason );
		put( "\n" );
	}
	put( "\nOpen:\n" );
	put( g_szContext[ 0 ] ? g_szContext : "(nothing)\n" );
	put( "\nStack:\n" );
	void *frames[ 64 ];
	const USHORT nFrames = CaptureStackBackTrace( 0, 64, frames, nullptr );
	for ( USHORT i = 0; i < nFrames; i++ )
	{
		HMODULE hModule = nullptr;
		char szModule[ MAX_PATH ] = "?";
		if ( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>( frames[ i ] ), &hModule ) )
			GetModuleFileNameA( hModule, szModule, MAX_PATH );
		std::snprintf( szLine, sizeof( szLine ), "%2u %p %s+0x%llx\n", static_cast<unsigned>( i ), frames[ i ], szModule, static_cast<unsigned long long>( static_cast<const char *>( frames[ i ] ) - reinterpret_cast<const char *>( hModule ) ) );
		put( szLine );
	}
	CloseHandle( hFile );
	return EXCEPTION_CONTINUE_SEARCH;
}

#endif

//-----------------------------------------------------------------------------
// Purpose: An exception nothing caught: its type and message kept for the log, then the
//          abort that writes it.
//-----------------------------------------------------------------------------
[[noreturn]] void OnTerminate()
{
	if ( const std::exception_ptr pException = std::current_exception() )
	{
		try
		{
			std::rethrow_exception( pException );
		}
		catch ( const std::exception &e )
		{
			std::snprintf( g_szReason, sizeof( g_szReason ), "An exception nothing caught: %s (%s)", e.what(), typeid( e ).name() );
		}
		catch ( ... )
		{
			std::snprintf( g_szReason, sizeof( g_szReason ), "An exception nothing caught, of an unknown type" );
		}
	}
	else
	{
		std::snprintf( g_szReason, sizeof( g_szReason ), "std::terminate was called" );
	}
#if defined( _WIN32 )
	// Windows' abort doesn't reach the exception filter: the log is written here.
	OnException( nullptr );
#endif
	std::abort();
}

//-----------------------------------------------------------------------------
// Purpose: The last lines of a text file.
//-----------------------------------------------------------------------------
QString Tail( const QString &szPath, int nLines )
{
	QFile file( szPath );
	if ( !file.open( QIODevice::ReadOnly ) )
		return {};
	if ( file.size() > 256 * 1024 )
		file.seek( file.size() - 256 * 1024 );
	QStringList lines = QString::fromUtf8( file.readAll() ).split( u'\n' );
	if ( lines.size() > nLines )
		lines = lines.mid( lines.size() - nLines );
	return lines.join( u'\n' );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: A throw out of an event: logged, shown once the event is over, and the app goes on.
//-----------------------------------------------------------------------------
bool CApplication::notify( QObject *pReceiver, QEvent *pEvent )
{
	QString szReason;
	try
	{
		return QApplication::notify( pReceiver, pEvent );
	}
	catch ( const std::exception &e )
	{
		szReason = QStringLiteral( "%1 (%2)" ).arg( QString::fromLocal8Bit( e.what() ), QString::fromLatin1( typeid( e ).name() ) );
	}
	catch ( ... )
	{
		szReason = QStringLiteral( "an exception of an unknown type" );
	}
	const QString szLog = WriteErrorLog( szReason );
	DebugLog() << "caught" << szReason << "from an event; log at" << szLog;
	QTimer::singleShot( 0, [ szReason, szLog ] {
		QMessageBox::critical( QApplication::activeWindow(), QApplication::translate( "CApplication", "Something Went Wrong" ),
			QApplication::translate( "CApplication", "vomit-tools ran into an error and stopped what it was doing:\n\n%1\n\nIt's still running, but save your work. A log is at %2." ).arg( szReason, QDir::toNativeSeparators( szLog ) ) );
	} );
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: The folder and version are kept where the handlers can reach them without Qt; on
//          Linux the handlers get a stack of their own, for a crash that ran out of stack.
//-----------------------------------------------------------------------------
void InstallCrashHandler()
{
	QDir{}.mkpath( CrashLogFolder() );
	Keep( g_szFolder, QDir::toNativeSeparators( CrashLogFolder() ).toLocal8Bit() );
	Keep( g_szVersion, QCoreApplication::applicationVersion().toUtf8() );
	std::set_terminate( OnTerminate );
#if defined( _WIN32 )
	SetUnhandledExceptionFilter( OnException );
#else
#if defined( VOMIT_TOOLS_BACKTRACE )
	// The first call loads what it needs, which a signal handler mustn't.
	void *frames[ 2 ];
	static_cast<void>( backtrace( frames, 2 ) );
#endif
	static std::vector<char> altStack( 256 * 1024 );
	stack_t stack{};
	stack.ss_sp = altStack.data();
	stack.ss_size = altStack.size();
	sigaltstack( &stack, nullptr );
	struct sigaction action{};
	action.sa_sigaction = OnSignal;
	action.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset( &action.sa_mask );
	for ( const int nSignal : { SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL } )
		sigaction( nSignal, &action, nullptr );
#endif
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetCrashContext( const QString &szContext )
{
	Keep( g_szContext, szContext.toUtf8() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CrashLogFolder()
{
	return QDir{ QStandardPaths::writableLocation( QStandardPaths::AppLocalDataLocation ) }.filePath( QStringLiteral( "crashlogs" ) );
}

//-----------------------------------------------------------------------------
// Purpose: The same as a crash's, but written the ordinary way: the app is still sound.
//-----------------------------------------------------------------------------
QString WriteErrorLog( const QString &szReason )
{
	QDir{}.mkpath( CrashLogFolder() );
	const QString szPath = QDir{ CrashLogFolder() }.filePath( QStringLiteral( "error-%1.txt" ).arg( QDateTime::currentSecsSinceEpoch() ) );
	QSaveFile file( szPath );
	if ( file.open( QIODevice::WriteOnly ) )
	{
		const QString szText = QStringLiteral( "vomit-tools %1 ran into an error and carried on: %2\n\nOpen:\n%3" ).arg( QCoreApplication::applicationVersion(), szReason, QString::fromUtf8( g_szContext ) );
		file.write( szText.toUtf8() );
		file.commit();
	}
	return szPath;
}

//-----------------------------------------------------------------------------
// Purpose: Crash logs newer than the last shown. The debug log, when it was on, ended close to
//          the crash: its last lines go into the log before the new run starts it again.
//-----------------------------------------------------------------------------
QStringList CollectCrashLogs()
{
	const qint64 nShownUntil = QSettings{}.value( QLatin1String( SHOWN_SETTING ), 0 ).toLongLong();
	QStringList logs;
	const QFileInfoList files = QDir{ CrashLogFolder() }.entryInfoList( { QStringLiteral( "crash-*.txt" ) }, QDir::Files, QDir::Time );
	const QFileInfo debugLog( DebugLogPath() );
	for ( const QFileInfo &file : files )
	{
		if ( file.lastModified().toMSecsSinceEpoch() <= nShownUntil )
			continue;
		const qint64 nGap = debugLog.exists() ? debugLog.lastModified().secsTo( file.lastModified() ) : -1;
		if ( logs.isEmpty() && nGap >= -5 && nGap <= 600 )
		{
			const QString szTail = Tail( debugLog.absoluteFilePath(), DEBUG_LOG_LINES );
			QFile out( file.absoluteFilePath() );
			if ( !szTail.isEmpty() && out.open( QIODevice::Append ) )
				out.write( ( QStringLiteral( "\nThe debug log's last lines:\n" ) + szTail + u'\n' ).toUtf8() );
		}
		logs << file.absoluteFilePath();
	}
	if ( !files.isEmpty() )
		QSettings{}.setValue( QLatin1String( SHOWN_SETTING ), QDateTime::currentMSecsSinceEpoch() );
	return logs;
}

//-----------------------------------------------------------------------------
// Purpose: The newest log in full; the others are in the folder.
//-----------------------------------------------------------------------------
void ShowCrashLogs( QWidget *pParent, const QStringList &logs )
{
	if ( logs.isEmpty() )
		return;
	QFile file( logs.front() );
	const QString szText = file.open( QIODevice::ReadOnly ) ? QString::fromUtf8( file.readAll() ) : QString{};
	QDialog dialog( pParent );
	dialog.setObjectName( QStringLiteral( "crashDialog" ) );
	dialog.setWindowTitle( QApplication::translate( "CApplication", "vomit-tools Closed Unexpectedly" ) );
	dialog.resize( 760, 520 );
	auto *pLayout = new QVBoxLayout( &dialog );
	auto *pIntro = new QLabel( logs.size() > 1 ? QApplication::translate( "CApplication", "vomit-tools closed unexpectedly (%1 times since it was last open). This is what it wrote the last time; maps you hadn't saved are offered back from their autosaves." ).arg( logs.size() )
											   : QApplication::translate( "CApplication", "vomit-tools closed unexpectedly last time. This is what it wrote; maps you hadn't saved are offered back from their autosaves." ) );
	pIntro->setWordWrap( true );
	pLayout->addWidget( pIntro );
	auto *pText = new QPlainTextEdit( szText );
	pText->setObjectName( QStringLiteral( "crashText" ) );
	pText->setReadOnly( true );
	pText->setLineWrapMode( QPlainTextEdit::NoWrap );
	pText->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
	pLayout->addWidget( pText, 1 );
	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Close );
	QPushButton *pCopy = pButtons->addButton( QApplication::translate( "CApplication", "Copy" ), QDialogButtonBox::ActionRole );
	QPushButton *pFolder = pButtons->addButton( QApplication::translate( "CApplication", "Open Folder" ), QDialogButtonBox::ActionRole );
	QObject::connect( pCopy, &QPushButton::clicked, &dialog, [ szText ] { QGuiApplication::clipboard()->setText( szText ); } );
	QObject::connect( pFolder, &QPushButton::clicked, &dialog, [] { QDesktopServices::openUrl( QUrl::fromLocalFile( CrashLogFolder() ) ); } );
	QObject::connect( pButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
	pLayout->addWidget( pButtons );
	dialog.exec();
}
