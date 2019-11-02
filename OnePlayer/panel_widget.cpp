#include "panel_widget.h"

PanelWidget::PanelWidget(QWidget *parent)
	: QWidget(parent)
{
	setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint | Qt::Tool);
	setAttribute(Qt::WA_TranslucentBackground);
	hide();
}

PanelWidget::~PanelWidget()
{
}

void PanelWidget::enterEvent(QEvent * event)
{
	emit enterPanel();
}

void PanelWidget::leaveEvent(QEvent * event)
{
	emit leavePanel();
}
