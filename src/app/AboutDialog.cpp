// SPDX-License-Identifier: GPL-3.0-or-later
#include "AboutDialog.h"

#include <algorithm>

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QSysInfo>
#include <QVBoxLayout>

#include "BuildInfo.h"

namespace {

constexpr auto REPOSITORY = "https://github.com/Verdessence/vomit-tools";

QString compilerName() {
#if defined(__clang__)
	return QStringLiteral("Clang %1.%2.%3").arg(__clang_major__).arg(__clang_minor__).arg(__clang_patchlevel__);
#elif defined(_MSC_VER)
	return QStringLiteral("MSVC %1").arg(_MSC_FULL_VER);
#elif defined(__GNUC__)
	return QStringLiteral("GCC %1.%2.%3").arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__);
#else
	return QStringLiteral("unknown compiler");
#endif
}

} // namespace

AboutDialog::AboutDialog(QWidget* parent)
		: QDialog(parent) {
	setWindowTitle(tr("About vomit-tools"));

	auto* name = new QLabel(QStringLiteral("vomit-tools"));
	QFont big = name->font();
	big.setPointSizeF(big.pointSizeF() * 1.8);
	big.setBold(true);
	name->setFont(big);

	auto* version = new QLabel(tr("Version %1").arg(versionText()));
	version->setObjectName(QStringLiteral("version"));
	version->setTextInteractionFlags(Qt::TextSelectableByMouse);

	auto* about = new QLabel(tr(
		"<p>Soundscape, skybox and particle editors for Source engine games.</p>"
		"<p>Source code and releases: <a href=\"%1\">%1</a></p>"
		"<p>Free software under the <a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GNU GPL, version 3 or later</a>. "
		"Built on <a href=\"https://www.qt.io\">Qt</a> (LGPLv3), "
		"<a href=\"https://github.com/craftablescience/sourcepp\">sourcepp</a> (MIT), "
		"<a href=\"https://miniaud.io\">miniaudio</a> (public domain / MIT-0) and "
		"<a href=\"https://github.com/nothings/stb\">stb</a> (public domain / MIT).</p>"
		"<p>Not affiliated with Valve. Source and Counter-Strike are trademarks of Valve Corporation.</p>")
		.arg(QLatin1StringView{REPOSITORY}));
	about->setWordWrap(true);
	about->setOpenExternalLinks(true);
	about->setTextInteractionFlags(Qt::TextBrowserInteraction);

	auto* details = new QLabel(buildDetails().section(u'\n', 1).replace(u'\n', QStringLiteral(" · ")));
	details->setObjectName(QStringLiteral("details"));
	details->setWordWrap(true);
	details->setEnabled(false);
	details->setTextInteractionFlags(Qt::TextSelectableByMouse);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	QPushButton* copy = buttons->addButton(tr("Copy Build Details"), QDialogButtonBox::ActionRole);
	copy->setToolTip(tr("Copies the version, Qt, system and compiler, for bug reports"));
	connect(copy, &QPushButton::clicked, this, [copy] {
		QApplication::clipboard()->setText(buildDetails());
		copy->setText(tr("Copied"));
	});
	QPushButton* aboutQt = buttons->addButton(tr("About Qt"), QDialogButtonBox::ActionRole);
	connect(aboutQt, &QPushButton::clicked, qApp, &QApplication::aboutQt);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto* layout = new QVBoxLayout(this);
	layout->setSpacing(10);
	layout->addWidget(name);
	layout->addWidget(version);
	layout->addWidget(about);
	layout->addWidget(details);
	layout->addSpacing(6);
	layout->addWidget(buttons);
	setFixedWidth(std::max(560, sizeHint().width()));
}

QString AboutDialog::versionText() {
	const QString version = QString::fromUtf8(buildinfo::version);
	const QString commit = QString::fromUtf8(buildinfo::commit);
	// A build of the release tag itself needs no commit: "v0.3.0" is 0.3.0.
	if (commit.isEmpty() || commit == u'v' + version) {
		return version;
	}
	return QStringLiteral("%1 (%2)").arg(version, commit);
}

QString AboutDialog::buildDetails() {
	QString qt = QStringLiteral("Qt %1").arg(QString::fromLatin1(qVersion()));
	if (qt != QStringLiteral("Qt " QT_VERSION_STR)) {
		qt += QStringLiteral(" (built with " QT_VERSION_STR ")");
	}
	return QStringLiteral("vomit-tools %1\n%2\n%3 %4\n%5")
		.arg(versionText(), qt, QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(), compilerName());
}
