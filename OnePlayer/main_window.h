#ifndef MAIN_WINDOW_H_
#define MAIN_WINDOW_H_

#include <QtWidgets/QMainWindow>
#include <QLabel>
#include <QListWidget>
#include "ui_main_window.h"
#include "panel_widget.h"
#include "player.h"

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	MainWindow(QWidget *parent = Q_NULLPTR);
	~MainWindow() = default;

protected:
	void closeEvent(QCloseEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	void keyReleaseEvent(QKeyEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void moveEvent(QMoveEvent *event) override;
	bool eventFilter(QObject *object, QEvent *event) override;

private slots:
	void openLocalFile();
	void openNetworkFile();
	void display();
	void buttonClicked();
	void stop();
	void pause();
	void mute();
	void dragUpdateVolume(int value);
	void sliderSeek(qint64 seconds);
	void updatedSliderByTimer();
	void sliderReleased();
	void setProgressSliderRange();
	void hidePanelByTimer();
	void stopPanelTimer();
	void startPanelTimer();
	void setAutoPlay();
	void setLoopPlay();
	void setNoMorePlay();
	void playAfterFinished();
	void playByClickPlaylist(QListWidgetItem *file_item);

private:
	Ui::MainWindowClass ui;
	QWidget *video_widget;
	PanelWidget *panel_widget;
	QDockWidget *playlist_dock;
	QListWidget *playlist_list;//playlist_dock里的条目
	QImage image;
	QMenu* m_menu;
	QMenu* r_menu;
	QAction *open_file_act;
	QAction *open_more_act;
	QAction *auto_play_act;
	QAction *loop_play_act;
	QAction *nomore_play_act;
	QAction *fullscreen_act;
	QAction *setting_act;
	QAction *about_act;
	QList<QAction*> act_li1, act_li2, act_li3;
	QString current_file;
	QToolButton *open_file_btn;
	QToolButton *playlist_btn;
	QToolButton *capture_btn;
	QToolButton *pause_btn;
	QToolButton *stop_btn;
	QToolButton *next_btn;
	QToolButton *prev_btn;
	QToolButton *mute_btn;
	QSlider *volume_slider;
	QSlider *progress_slider;
	QLabel *duration_label;
	QTimer *progress_timer;
	QTimer *panel_timer; //控制栏和进度条显隐
	//QVector<QWidget *> hide_widgets;
	QList<QString> playlist;    // list to stroe video files in same path


	Player *player;
	MediaState *ms;
	bool panel_visible;
	bool auto_play;          // switch to control whether to continue to playing other file
	bool loop_play;          // switch to control whether to continue to playing same file
	bool is_muted;			 // 从用户角度考虑，可能播放视频前就需要先设置静音和音量
	int volume;
	bool init_panel;		 // first time to show panel

	void initFFmpegSDL();
	void initUI();
	void createActions();
	void createMenus();
	void createWidgets();
	void createConnect();
	void openFile(QString file_name);
	inline QString getFilenameFromPath(QString path);
	void addVideoToList(QString path);
	void playNext();
	void playPrevious();
	void showPanel(bool show);
	void capture();
	void writeSettings();
	void readSettings();
	
signals:
	void stopVideo();
	void stopPlayer();
	void pauseVideo();
	void seekVideo(double incr);
	void muteVideo();
	void fileSelected(MediaState *ms);
};

#endif // !MAIN_WINDOW_H_