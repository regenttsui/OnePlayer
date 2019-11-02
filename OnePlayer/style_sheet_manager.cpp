#include <QDebug>
#include <QDir>
#include <QFile>
#include <QApplication>
#include "style_sheet_manager.h"

StyleSheetManager::StyleSheetManager(const QString & filePath, QObject * parent)
	: QObject(parent)
{
	setFilePath(filePath);
}

QString StyleSheetManager::getfilePath() const
{
	return qss_file_path;
}

bool StyleSheetManager::load(const QString & styleStr)
{
	if (!styleStr.isEmpty() && qApp)
	{
		qApp->setStyleSheet(styleStr);
		setFilePath(styleStr);
		return true;
	}
	return false;
}

bool StyleSheetManager::loadFile(const QString & path)
{
	QString styleStr = readFile(path);
	return load(styleStr);
}

bool StyleSheetManager::reload()
{
	return loadFile(getfilePath());
}

bool StyleSheetManager::switchTo(const QString & path)
{
	if (getfilePath() == path)
	{
		return false;
	}
	return loadFile(path);
}

void StyleSheetManager::setFilePath(const QString & path)
{
	qss_file_path = path;
}

QString StyleSheetManager::readFile(const QString & path) const
{
	QString ret;
	QFile file(path);
	if (file.open(QIODevice::ReadOnly))
	{
		ret = file.readAll();
		file.close();
	}
	return ret;
}
