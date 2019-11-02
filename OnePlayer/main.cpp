#include "main_window.h"
#include "style_sheet_manager.h"
#include <QtWidgets/QApplication>
#include <QDebug>
int main(int argc, char *argv[])
{
	QApplication a(argc, argv);
	StyleSheetManager manager;
	manager.loadFile(":/Resources/style.qss");

	MainWindow w;
	w.show();

	return a.exec();
}
