// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QCommandLineParser>
#include <QSettings>

#include "MainWindow.h"

int main(int argc, char* argv[]) {
	QApplication app{argc, argv};
	QApplication::setApplicationName(QStringLiteral("vomit-tools"));
	QApplication::setOrganizationName(QStringLiteral("vomit-tools"));
	QApplication::setApplicationVersion(QStringLiteral(VOMIT_TOOLS_VERSION));

	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("Soundscape, skybox and particle editors for Source engine games"));
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addPositionalArgument(QStringLiteral("paths"), QStringLiteral("Files, folders or a game's gameinfo.txt to open"), QStringLiteral("[paths...]"));
	const QCommandLineOption edit{{QStringLiteral("e"), QStringLiteral("edit")},
	                              QStringLiteral("Open a soundscape or soundscript from the game, e.g. scripts/soundscapes_dust2.txt"),
	                              QStringLiteral("file")};
	parser.addOption(edit);
	parser.process(app);

	MainWindow window;
	window.show();

	const QStringList paths = parser.positionalArguments();
	if (paths.isEmpty()) {
		if (const QString game = QSettings{}.value(QStringLiteral("lastGameInfo")).toString(); !game.isEmpty()) {
			window.openGame(game);
		}
	}
	for (const QString& path : paths) {
		window.openPath(path);
	}
	for (const QString& file : parser.values(edit)) {
		window.openSoundFile(file);
	}
	return QApplication::exec();
}
