#pragma once

#include <QWidget>

class PanelWidget : public QWidget
{
	Q_OBJECT

public:
	PanelWidget(QWidget *parent);
	~PanelWidget();

protected:
	void enterEvent(QEvent *event) override;
	void leaveEvent(QEvent *event) override;

signals:
	void enterPanel();
	void leavePanel();
};
