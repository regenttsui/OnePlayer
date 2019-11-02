#include <QtWidgets>
#include "main_window.h"

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent)
{
	ui.setupUi(this);
	setWindowTitle("OnePlayer");
	setWindowIcon(QIcon(QStringLiteral(":/Resources/OnePlayer.png")));
	setFocus();
	//setWindowFlags(Qt::FramelessWindowHint | windowFlags());
	video_widget = new QWidget(this);
	setCentralWidget(video_widget);
	//解决界面中存在按钮时焦点默认在按钮上，此时空格键、回车键、方向键以及tab键均无法获取到的问题
	//video_widget->setFocusPolicy(Qt::StrongFocus);
	video_widget->setUpdatesEnabled(false); //避免右键、调整窗口大小等造成的各种闪烁,但是又会导致panel完全无法显示..

	panel_widget = new PanelWidget(this);
	playlist_dock = new QDockWidget(this);
	playlist_list = new QListWidget(playlist_dock);

	initFFmpegSDL();

	player = new Player;
	progress_timer = new QTimer;
	panel_timer = new QTimer;
	panel_timer->start(2000);

	createActions();
	//createMenus();
	createWidgets();
	createConnect();
	image = QImage("./Resources/index.png");

	initUI();
	panel_visible = false;
	//auto_play = true;
	//is_muted = false;
	//volume = STARTUP_VOLUME;
	init_panel = false;
	readSettings();
}

void MainWindow::closeEvent(QCloseEvent * event)
{
	const QMessageBox::StandardButton ret = QMessageBox::warning(this, "OnePlayer", "确认退出OnePlayer?",
		QMessageBox::Yes | QMessageBox::No);

	//QApplication::setOverrideCursor(Qt::ArrowCursor);
	if (ret == QMessageBox::Yes)
	{
		writeSettings();
		if (!player->isStop() && ms)
		{
			stream_close(ms);
		}
		do_exit();
		event->accept();
	}
	else
	{
		event->ignore();
	}
}

//右键菜单需要重载的函数
void MainWindow::contextMenuEvent(QContextMenuEvent * event)
{
	QMenu menu(this);
	menu.addActions(act_li1);
	QMenu *open_more_menu = menu.addMenu("打开");
	open_more_menu->addAction(open_more_act);
	menu.addSeparator();

	menu.addActions(act_li2);
	menu.addSeparator();

	menu.addActions(act_li3);

	menu.exec(event->globalPos());
}

void MainWindow::resizeEvent(QResizeEvent * event)
{
	panel_widget->setGeometry(this->geometry().x(), this->geometry().y() + video_widget->height() - 150,
		video_widget->width(), 150);
	panel_widget->show();
	if (player->isStop())
		return;
	//sdl窗口相应调整大小
	if (ms)
	{
		ms->width = video_widget->width();
		ms->height = video_widget->height();
		ms->force_refresh = 1;//强制立马刷新
	}
}

void MainWindow::paintEvent(QPaintEvent * event)
{
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	SDL_Rect rect = { 0,0,video_widget->width(),video_widget->height() };
	SDL_RenderPresent(renderer); //渲染输出画面
	SDL_RenderFillRect(renderer, &rect);
	//QPainter painter(video_widget);
	//painter.setRenderHint(QPainter::Antialiasing);
	//painter.setBrush(Qt::black);
	//painter.drawRect(0, 0, video_widget->width(), video_widget->height()); //先画成黑色

	////将图像按比例缩放成和窗口一样大小
	//image = image.scaled(video_widget->size(), Qt::KeepAspectRatio);

	//int x = (video_widget->width() - image.width()) / 2;
	//int y = (video_widget->height() - image.height()) / 2;

	//painter.drawImage(QPoint(x, y), image); //画出图像
}


void MainWindow::keyReleaseEvent(QKeyEvent * event)
{
	double progress_incr;
	int vol_per;
	switch (event->key())
	{
	case Qt::Key_Up:
		vol_per = ms->audio_volume * 100 / SDL_MIX_MAXVOLUME + SDL_VOLUME_PER;
		ms->audio_volume = av_clip(SDL_MIX_MAXVOLUME * vol_per / 100, 0, SDL_MIX_MAXVOLUME);
		volume_slider->setValue(vol_per > 100 ? 100 : vol_per);
		break;
	case Qt::Key_Down:
		vol_per = ms->audio_volume * 100 / SDL_MIX_MAXVOLUME - SDL_VOLUME_PER;
		ms->audio_volume = av_clip(SDL_MIX_MAXVOLUME * vol_per / 100, 0, SDL_MIX_MAXVOLUME);
		volume_slider->setValue(vol_per < 0 ? 0 : vol_per);
		break;
	case Qt::Key_Left:
		if (player->isStop())
			break;
		progress_incr = seek_interval ? -seek_interval : -5.0;
		emit seekVideo(progress_incr);
		break;
	case Qt::Key_Right:
		if (player->isStop())
			break;
		progress_incr = seek_interval ? seek_interval : 5.0;
		emit seekVideo(progress_incr);
		break;
	case Qt::Key_S:
		//逐帧播放或暂停时的seek
		if (player->isStop())
			break;
		if (ms->paused)
			stream_toggle_pause(ms);
		ms->step = 1;
		break;
	case Qt::Key_Escape:
		showNormal();
		break;
	case Qt::Key_Space:
		emit pauseVideo();
		break;
	case Qt::Key_Enter:
	case Qt::Key_Return:
	case Qt::Key_F11:
		if (isFullScreen())
			showNormal();
		else
			showFullScreen();
		break;
	default:
		break;
	}
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent * event)
{
	if (event->buttons() == Qt::LeftButton)
	{
		if (isFullScreen())
			showNormal();
		else
			showFullScreen(); //windows的OpenGLwidget有bug,全屏无法弹出右键菜单
	}
}

void MainWindow::mouseMoveEvent(QMouseEvent * event)
{
	if (!init_panel)
	{
		panel_widget->setGeometry(this->geometry().x(), this->geometry().y() + video_widget->height() - 150,
			video_widget->width(), 150);
		init_panel = true;
		repaint();
	}

	if (!player->isStop() && this->updatesEnabled() && playlist_dock->isHidden())
		this->setUpdatesEnabled(false);

	panel_timer->stop();
	if (!panel_visible)
	{
		showPanel(true);
		panel_visible = true;
		QApplication::setOverrideCursor(Qt::ArrowCursor);//show mouse
	}
	panel_timer->start();
}

void MainWindow::moveEvent(QMoveEvent * event)
{
	QPoint pos = event->pos();
	panel_widget->move(pos.x(), pos.y() + video_widget->height() - 150);
}

//点击进度条
bool MainWindow::eventFilter(QObject * object, QEvent * event)
{
	if (object == progress_slider)
	{
		if (event->type() == QEvent::MouseButtonPress && !player->isStop())
		{
			QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
			if (mouseEvent->button() == Qt::LeftButton)
			{
				progress_timer->stop();
				int duration = progress_slider->maximum() - progress_slider->minimum();
				int pos = progress_slider->minimum() + duration * (static_cast<double>(mouseEvent->x()) / progress_slider->width());
				if (pos != progress_slider->sliderPosition())
				{
					progress_slider->setValue(pos);
					stream_seek(ms, static_cast<qint64>(pos) * AV_TIME_BASE, 0, 0);
				}
				progress_timer->start();
			}
		}
	}
	else if (object == volume_slider)
	{
		if (event->type() == QEvent::MouseButtonPress)
		{
			QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
			if (mouseEvent->button() == Qt::LeftButton)
			{
				int pos = volume_slider->minimum() + 100 * (static_cast<double>(mouseEvent->x()) / volume_slider->width());
				if (pos != volume_slider->sliderPosition())
				{
					volume_slider->setValue(pos);
					volume = av_clip(SDL_MIX_MAXVOLUME * pos / 100, 0, SDL_MIX_MAXVOLUME);
					if (!player->isStop())
						ms->audio_volume = volume;
				}
			}
		}
	}

	return QObject::eventFilter(object, event);
}


void MainWindow::openLocalFile()
{
	current_file = QFileDialog::getOpenFileName(this, "打开媒体文件", "C:/Users", "Media (*.mp4 *.avi *.mkv *.rmvb *.flv *.mpg *.wmv *.mov *.ts *.264 *.265 *.wav *.flac *.ape *.mp3)");
	if (!current_file.isEmpty())
	{
		QString path = current_file.left(current_file.lastIndexOf("/") + 1);
		addVideoToList(path);
		//setWindowTitle(getFilenameFromPath(current_file));
		openFile(current_file);
	}

}

void MainWindow::openNetworkFile()
{
	bool ok;
	QString text = QInputDialog::getText(this, "打开网络文件", "请输入网络文件的地址", QLineEdit::Normal, QString(), &ok);
	if (ok && !text.isEmpty())
	{
		//setWindowTitle(text);
		openFile(text);
		progress_timer->stop();//网络文件进度条不动
	}
}

void MainWindow::display()
{
	if (!ms)
		return;

	//如果窗口未显示，则显示窗口
	if (!ms->width)
	{
		ms->width = video_widget->width();
		ms->height = video_widget->height();
	}

	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	//纹理处理
	if (ms->audio_st && ms->show_mode != SHOW_MODE_VIDEO)
		video_audio_display(ms); //显示仅有音频的文件
	else if (ms->video_st)
		video_image_display(ms); //显示一帧视频画面
	SDL_RenderPresent(renderer); //渲染输出画面
}

void MainWindow::buttonClicked()
{
	if (QObject::sender() == stop_btn)
	{
		this->setUpdatesEnabled(true);
		emit stopVideo();
		progress_timer->stop();
		duration_label->setText(QString("00:00:00 / 00:00:00"));
		progress_slider->setValue(0);
	}
	else if (QObject::sender() == pause_btn)
	{
		emit pauseVideo();
	}
	else if (QObject::sender() == prev_btn)
	{
		if (player->isStop())
			return;
		playPrevious();
	}
	else if (QObject::sender() == next_btn)
	{
		if (player->isStop())
			return;
		playNext();
	}
	else if (QObject::sender() == mute_btn)
	{
		emit muteVideo();
	}
	else if (QObject::sender() == capture_btn)
	{
		if (player->isStop())
			return;
		//emit pauseVideo();
		capture();
		//emit pauseVideo();
	}
	else if (QObject::sender() == playlist_btn)
	{
		this->setUpdatesEnabled(true);
		if (playlist_dock->isHidden())
		{
			playlist_dock->show();
		}
		else
		{
			playlist_dock->hide();
		}

	}
}

void MainWindow::stop()
{
	if (player->isStop())
		return;
	emit stopPlayer();

	if (ms)
	{
		stream_close(ms);
	}
	//image = QImage("./Resources/index.png");
	update();
}

void MainWindow::pause()
{
	if (player->isStop())
		return;
	toggle_pause(ms);
	if (progress_timer->isActive())
	{
		progress_timer->stop();
		pause_btn->setToolTip("播放");
		pause_btn->setIcon(QIcon(":/Resources/play.png"));
	}
	else
	{
		progress_timer->start();
		pause_btn->setToolTip("暂停");
		pause_btn->setIcon(QIcon(":/Resources/pause.png"));
	}

}

void MainWindow::mute()
{
	is_muted = !is_muted;
	if (is_muted)
		mute_btn->setIcon(QIcon(":/Resources/mute.png"));
	else
		mute_btn->setIcon(QIcon(":/Resources/not_mute.png"));

	if (player->isStop())
		return;
	toggle_mute(ms);
}

void MainWindow::dragUpdateVolume(int value)
{
	volume = av_clip(SDL_MIX_MAXVOLUME * value / 100, 0, SDL_MIX_MAXVOLUME);
	if (player->isStop())
		return;
	ms->audio_volume = volume;
}

void MainWindow::setProgressSliderRange()
{
	progress_slider->setMaximum(ms->ic->duration / 1000000LL);
}

void MainWindow::hidePanelByTimer()
{
	if (panel_visible)
	{
		QApplication::setOverrideCursor(Qt::BlankCursor);//hide mouse
		showPanel(false);
		panel_visible = false;
	}
}

void MainWindow::stopPanelTimer()
{
	panel_timer->stop();
}

void MainWindow::startPanelTimer()
{
	panel_timer->start();
}

void MainWindow::playAfterFinished()
{
	if (auto_play)
	{
		playNext();
	}
	else if (loop_play)
	{
		openFile(current_file);
	}
	else
	{
		this->setUpdatesEnabled(true);
		emit stopVideo();
		progress_timer->stop();
		duration_label->setText(QString("00:00:00 / 00:00:00"));
		progress_slider->setValue(0);
	}
}

void MainWindow::playByClickPlaylist(QListWidgetItem * file_item)
{
	current_file = file_item->text();
	openFile(current_file);
}


void MainWindow::initFFmpegSDL()
{
	av_log_set_flags(AV_LOG_SKIP_REPEATED);

	//avfilter_register_all();
	//avdevice_register_all();
	avformat_network_init();

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		qDebug("Could not initialize SDL - %s\n", SDL_GetError());
		exit(1);
	}

	win = SDL_CreateWindowFrom((void*)video_widget->winId());
	//设置缩放的算法级别
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	//创建渲染器
	if (win)
	{
		renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
		if (!renderer)
		{
			qDebug("Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
			renderer = SDL_CreateRenderer(win, -1, 0);
		}
		if (renderer)
		{
			if (!SDL_GetRendererInfo(renderer, &renderer_info))
				qDebug("Initialized %s renderer.\n", renderer_info.name);
		}
	}
	if (!win || !renderer || !renderer_info.num_texture_formats)
	{
		qDebug("Failed to create window or renderer: %s", SDL_GetError());
		do_exit();
	}
}

void MainWindow::initUI()
{
	ui.statusBar->close();	//关闭状态栏和工具栏
	ui.mainToolBar->close();
	ui.mainMenuBar->close();
	this->setMinimumSize(QSize(890, 535));
	this->setMouseTracking(true);
	video_widget->setMouseTracking(true);

	//hide_widgets.push_back(open_file_btn);
	//hide_widgets.push_back(playlist_btn);
	//hide_widgets.push_back(capture_btn);
	//hide_widgets.push_back(pause_btn);
	//hide_widgets.push_back(stop_btn);
	//hide_widgets.push_back(next_btn);
	//hide_widgets.push_back(prev_btn);
	//hide_widgets.push_back(mute_btn);
	//hide_widgets.push_back(volume_slider);
	//hide_widgets.push_back(progress_slider);
	//hide_widgets.push_back(duration_label);

	progress_slider->installEventFilter(this);
	volume_slider->installEventFilter(this);

	playlist_dock->setAllowedAreas(Qt::RightDockWidgetArea);
	playlist_dock->setFeatures(QDockWidget::AllDockWidgetFeatures);
	playlist_dock->setWindowTitle("播放列表");
	playlist_dock->setWidget(playlist_list);
	addDockWidget(Qt::RightDockWidgetArea, playlist_dock);
	playlist_dock->hide();

}

void MainWindow::createActions()
{
	open_file_act = new QAction("打开文件...");
	open_file_act->setShortcut(QKeySequence::Open);
	connect(open_file_act, &QAction::triggered, this, &MainWindow::openLocalFile);
	act_li1 << open_file_act;

	open_more_act = new QAction("打开网络文件");
	connect(open_more_act, &QAction::triggered, this, &MainWindow::openNetworkFile);

	auto_play_act = new QAction("连续播放");
	auto_play_act->setCheckable(true);
	auto_play_act->setChecked(true);
	connect(auto_play_act, &QAction::triggered, this, &MainWindow::setAutoPlay);

	loop_play_act = new QAction("循环播放");
	loop_play_act->setCheckable(true);
	loop_play_act->setChecked(false);
	connect(loop_play_act, &QAction::triggered, this, &MainWindow::setLoopPlay);

	nomore_play_act = new QAction("播放完成停止");
	nomore_play_act->setCheckable(true);
	nomore_play_act->setChecked(false);
	connect(nomore_play_act, &QAction::triggered, this, &MainWindow::setNoMorePlay);

	QActionGroup *play_act_group = new QActionGroup(this);
	auto_play_act->setActionGroup(play_act_group);
	loop_play_act->setActionGroup(play_act_group);
	nomore_play_act->setActionGroup(play_act_group);

	fullscreen_act = new QAction("全屏");
	fullscreen_act->setShortcut(QKeySequence::FullScreen);
	connect(fullscreen_act, &QAction::triggered, this, &MainWindow::showFullScreen);
	act_li2 << auto_play_act << loop_play_act << nomore_play_act << fullscreen_act;

	setting_act = new QAction("设置");
	setting_act->setShortcut(QString("F5"));

	about_act = new QAction("关于...");
	about_act->setShortcut(QString("F1"));
	act_li3 << setting_act << about_act;

}

void MainWindow::createMenus()
{
	m_menu = menuBar()->addMenu("OnePlayer"); //菜单栏中的主菜单

	m_menu->addActions(act_li1);

	QMenu *open_more_menu = m_menu->addMenu("打开");
	open_more_menu->addAction(open_more_act);
	m_menu->addSeparator();

	m_menu->addActions(act_li2);
	m_menu->addSeparator();

	m_menu->addActions(act_li3);
}

void MainWindow::createWidgets()
{
	open_file_btn = new QToolButton();
	open_file_btn->setToolTip("打开文件");
	connect(open_file_btn, &QAbstractButton::clicked, this, &MainWindow::openLocalFile);
	open_file_btn->setIcon(QIcon(":/Resources/open_file.png"));
	open_file_btn->setIconSize(QSize(32, 32));

	capture_btn = new QToolButton();
	capture_btn->setToolTip("截图");
	connect(capture_btn, &QAbstractButton::clicked, this, &MainWindow::buttonClicked);
	capture_btn->setIcon(QIcon(":/Resources/capture.png"));
	capture_btn->setIconSize(QSize(32, 32));

	playlist_btn = new QToolButton();
	playlist_btn->setToolTip("打开/隐藏播放列表");
	playlist_btn->setIcon(QIcon(":/Resources/playlist.png"));
	playlist_btn->setIconSize(QSize(32, 32));
	connect(playlist_btn, &QAbstractButton::clicked, this, &MainWindow::buttonClicked);

	pause_btn = new QToolButton();
	pause_btn->setToolTip("播放");
	pause_btn->setIcon(QIcon(":/Resources/play.png"));
	pause_btn->setIconSize(QSize(48, 48));
	connect(pause_btn, &QAbstractButton::clicked, this, &MainWindow::buttonClicked);

	stop_btn = new QToolButton();
	stop_btn->setToolTip("停止播放");
	stop_btn->setIcon(QIcon(":/Resources/stop.png"));
	stop_btn->setIconSize(QSize(32, 32));
	connect(stop_btn, &QAbstractButton::clicked, this, &MainWindow::buttonClicked);

	next_btn = new QToolButton();
	next_btn->setToolTip("下一视频");
	next_btn->setIcon(QIcon(":/Resources/next.png"));
	next_btn->setIconSize(QSize(32, 32));
	connect(next_btn, &QAbstractButton::clicked, this, &MainWindow::buttonClicked);

	prev_btn = new QToolButton();
	prev_btn->setToolTip("上一视频");
	prev_btn->setIcon(QIcon(":/Resources/prev.png"));
	prev_btn->setIconSize(QSize(32, 32));
	connect(prev_btn, &QAbstractButton::clicked, this, &MainWindow::buttonClicked);

	mute_btn = new QToolButton();
	mute_btn->setToolTip("静音On/off");
	//mute_btn->setIcon(QIcon(":/Resources/not_mute.png"));
	mute_btn->setIconSize(QSize(32, 32));
	connect(mute_btn, &QAbstractButton::clicked, this, &MainWindow::buttonClicked);

	volume_slider = new QSlider(Qt::Horizontal);
	volume_slider->setRange(0, 100);
	volume_slider->setFixedWidth(100);
	//volume_slider->setValue(STARTUP_VOLUME);
	connect(volume_slider, &QSlider::valueChanged, this, &MainWindow::dragUpdateVolume);

	progress_slider = new QSlider(Qt::Horizontal);
	connect(progress_slider, &QSlider::sliderMoved, this, &MainWindow::sliderSeek);
	connect(progress_slider, &QSlider::sliderReleased, this, &MainWindow::sliderReleased);

	duration_label = new QLabel("00:00:00 / 00:00:00");
	QPalette pe;
	pe.setColor(QPalette::WindowText, QColor(202, 202, 202));
	duration_label->setPalette(pe);


	QBoxLayout *layout = new QVBoxLayout;
	QBoxLayout *control_layout = new QHBoxLayout;
	QBoxLayout *progress_layout = new QHBoxLayout;
	progress_layout->addWidget(progress_slider);
	progress_layout->addWidget(duration_label);

	control_layout->addWidget(open_file_btn);
	control_layout->addSpacing(20);
	control_layout->addWidget(capture_btn);
	control_layout->addSpacing(20);
	control_layout->addWidget(playlist_btn);
	control_layout->addStretch();
	control_layout->addWidget(prev_btn);
	control_layout->addSpacing(20);
	control_layout->addWidget(pause_btn);
	control_layout->addWidget(stop_btn);
	control_layout->addSpacing(20);
	control_layout->addWidget(next_btn);
	control_layout->addSpacing(20);
	control_layout->addWidget(mute_btn);
	control_layout->addWidget(volume_slider);

	//this->centralWidget()->setLayout(layout);
	panel_widget->setLayout(layout);
	//layout->addSpacerItem(&QSpacerItem(video_widget->width(), 800, QSizePolicy::Expanding));
	layout->addStretch();
	layout->addLayout(progress_layout);
	layout->addSpacing(20);
	layout->addLayout(control_layout);
	layout->setContentsMargins(50, 10, 50, 50);

}

void MainWindow::createConnect()
{
	connect(this, &MainWindow::fileSelected, player, &Player::play);
	connect(player, &Player::displayOneFrame, this, &MainWindow::display);
	connect(this, &MainWindow::pauseVideo, this, &MainWindow::pause);
	connect(this, &MainWindow::muteVideo, this, &MainWindow::mute);
	connect(this, &MainWindow::stopVideo, this, &MainWindow::stop);
	connect(this, &MainWindow::stopPlayer, player, &Player::stop);
	connect(this, &MainWindow::seekVideo, player, &Player::seek);
	connect(player, &Player::finished, this, &MainWindow::playAfterFinished);
	connect(player, &Player::gotDuration, this, &MainWindow::setProgressSliderRange);
	connect(progress_timer, &QTimer::timeout, this, &MainWindow::updatedSliderByTimer);
	connect(panel_timer, &QTimer::timeout, this, &MainWindow::hidePanelByTimer);
	connect(panel_widget, &PanelWidget::enterPanel, this, &MainWindow::stopPanelTimer);
	connect(panel_widget, &PanelWidget::leavePanel, this, &MainWindow::startPanelTimer);
}

void MainWindow::openFile(QString file_name)
{
	//先停止当前的视频
	//if (ms)
	//	emit stopVideo();
	if (!player->isStop())
	{
		emit stopPlayer();
		if (ms)
		{
			stream_close(ms);
		}
	}

	setWindowTitle(getFilenameFromPath(file_name));
	playlist_list->setItemSelected(playlist_list->findItems(file_name, Qt::MatchExactly)[0], true);
	std::string str(file_name.toLocal8Bit()); //正确转换字符串，避免中文乱码
	input_filename = str.c_str();

	av_init_packet(&flush_pkt);
	flush_pkt.data = (uint8_t *)&flush_pkt;
	// 打开流
	ms = stream_open(input_filename);
	if (!ms)
	{
		qDebug("Failed to initialize MediaState!\n");
		do_exit();
	}
	if (is_muted)
	{
		ms->audio_volume = 0;
		ms->muted = 1;
	}
	progress_timer->start(1000);
	pause_btn->setToolTip("暂停");
	pause_btn->setIcon(QIcon(":/Resources/pause.png"));
	if (volume != STARTUP_VOLUME)
	{
		ms->audio_volume = volume;
	}
	this->setUpdatesEnabled(false);
	emit fileSelected(ms);
}

inline QString MainWindow::getFilenameFromPath(QString path)
{
	return path.right(path.size() - path.lastIndexOf("/") - 1);
}

void MainWindow::addVideoToList(QString path)
{
	QDir dir(path);

	QRegExp re(".*\\.(|mp4|avi|mkv|rmvb|flv|mpg|wmv|mov|ts|264|265|wav|flac|ape|mp3)$");

	QFileInfoList list = dir.entryInfoList(QDir::Files);//返回目录中所有文件的QFileInfo对象列表
	for (int i = 0; i < list.size(); i++)
	{
		QFileInfo file_info = list[i];

		if (re.exactMatch(file_info.fileName()))
		{
			QString filename = getFilenameFromPath(file_info.fileName());
			/* avoid adding repeat file */
			if (!playlist.contains(filename))
			{
				playlist.push_back(file_info.absoluteFilePath());
			}
		}
	}
	playlist_list->addItems(playlist);
	connect(playlist_list, &QListWidget::itemDoubleClicked,
		this, &MainWindow::playByClickPlaylist);
}

void MainWindow::playNext()
{
	int video_num = playlist.size();
	if (video_num <= 0)
		return;

	int play_idx = 0;
	int curr_idx = playlist.indexOf(current_file);


	/* if current file index greater than 0, means have priview video
	 * play last index video, otherwise if this file is head,
	 * play tail index video
	 */
	if (curr_idx != video_num - 1)
	{
		play_idx = curr_idx + 1;
	}

	QString next_video = playlist.at(play_idx);

	/* check file whether exists */
	QFile file(next_video);
	if (!file.exists())
	{
		playlist.removeAt(play_idx);
		return;
	}

	current_file = next_video;
	openFile(next_video);
}

void MainWindow::playPrevious()
{
	int video_num = playlist.size();
	if (video_num <= 0)
		return;

	int curr_idx = playlist.indexOf(current_file);
	int play_idx = curr_idx - 1;


	/* if current file index greater than 0, means have priview video
	 * play last index video, otherwise if this file is head,
	 * play tail index video
	 */
	if (curr_idx == 0)
	{
		play_idx = video_num - 1;
	}

	QString prev_video = playlist.at(play_idx);

	/* check file whether exists */
	QFile file(prev_video);
	if (!file.exists())
	{
		playlist.removeAt(play_idx);
		return;
	}

	current_file = prev_video;
	openFile(prev_video);
}

void MainWindow::showPanel(bool show)
{
	if (show)
	{
		panel_widget->show();
		//for (QWidget *widget : hide_widgets)
		//	widget->show();
	}
	else
	{
		panel_widget->hide();
		//for (QWidget *widget : hide_widgets)
		//	widget->hide();
	}
}

void MainWindow::capture()
{
	AVFrame *frame_RGB = av_frame_alloc();
	if (av_image_alloc(frame_RGB->data, frame_RGB->linesize, ms->curr_frame.width, ms->curr_frame.height, AV_PIX_FMT_RGB32, 1) < 0)
		qDebug("image allocation failed");
	struct SwsContext *img_cvt_ctx;
	img_cvt_ctx = sws_getContext(ms->curr_frame.width, ms->curr_frame.height, (AVPixelFormat)ms->curr_frame.format,
		ms->curr_frame.width, ms->curr_frame.height, AV_PIX_FMT_RGB32, sws_flags, NULL, NULL, NULL);
	if (img_cvt_ctx)
	{
		sws_scale(img_cvt_ctx, (uint8_t const * const *)ms->curr_frame.data, ms->curr_frame.linesize, 0, ms->curr_frame.height,
			frame_RGB->data, frame_RGB->linesize);
	}
	QImage tmp_img((uchar *)frame_RGB->data[0], ms->curr_frame.width, ms->curr_frame.height, frame_RGB->linesize[0], QImage::Format_RGB32);
	QString capture_name = QFileDialog::getSaveFileName(this, "保存截图", current_file + "_untitled.jpg", "图片 (*.jpg *.png *.bmp)");
	tmp_img.save(capture_name);
	av_frame_free(&frame_RGB);
}

void MainWindow::writeSettings()
{
	QSettings settings("Regent Soft", "OnePlayer");

	settings.beginGroup("MainWindow");
	settings.setValue("size", size());
	settings.setValue("pos", pos());
	settings.setValue("is_mute", is_muted);
	settings.setValue("volume", volume);
	settings.setValue("auto_play", auto_play);
	settings.setValue("loop_play", loop_play);
	settings.endGroup();
}

void MainWindow::readSettings()
{
	QSettings settings("Regent Soft", "OnePlayer");

	settings.beginGroup("MainWindow");
	resize(settings.value("size", QSize(890, 535)).toSize());
	move(settings.value("pos", QPoint(500, 200)).toPoint());
	video_widget->repaint();
	is_muted = settings.value("is_mute", false).toBool();
	if (is_muted)
		mute_btn->setIcon(QIcon(":/Resources/mute.png"));
	else
		mute_btn->setIcon(QIcon(":/Resources/not_mute.png"));
	volume = settings.value("volume", STARTUP_VOLUME).toInt();
	volume_slider->setValue(volume);
	auto_play = settings.value("auto_play", true).toBool();
	loop_play = settings.value("loop_play", false).toBool();
	auto_play_act->setChecked(auto_play);
	loop_play_act->setChecked(loop_play);
	nomore_play_act->setChecked(!auto_play && !loop_play);
	settings.endGroup();
}

void MainWindow::setAutoPlay()
{
	auto_play = !auto_play;
	loop_play = false;
}

void MainWindow::setLoopPlay()
{
	loop_play = !loop_play;
	auto_play = false;
}

void MainWindow::setNoMorePlay()
{
	auto_play = loop_play = false;
}

void MainWindow::sliderSeek(qint64 seconds)
{
	if (player->isStop())
		return;

	progress_timer->stop();
	qint64 cur_info, total_dur;
	total_dur = static_cast<qint64>(ms->ic->duration / 1000000LL); //long long变量，将视频总时长转换为秒
	cur_info = seconds;
	QTime cur_time((cur_info / 3600) % 60, (cur_info / 60) % 60,
		cur_info % 60, (cur_info * 1000) % 1000);
	QTime total_time((total_dur / 3600) % 60, (total_dur / 60) % 60,
		total_dur % 60, (total_dur * 1000) % 1000);
	QString format = "mm:ss";
	if (total_dur > 3600)
		format = "hh:mm:ss";

	QString label_str(cur_time.toString(format) + " / " + total_time.toString(format));
	duration_label->setText(label_str);
	stream_seek(ms, cur_info*AV_TIME_BASE, 0, 0); //注意这里转为微秒进行seek

	//progress_slider->setValue(cur_info);
	//int duration = progress_slider->maximum() - progress_slider->minimum();
	//int pos = progress_slider->minimum() + duration * (static_cast<double>(mouseEvent->x()) / progress_slider->width());
	//if (pos != progress_slider->sliderPosition())
	//{
	//	progress_slider->setValue(pos);
	//	decoder->seekProgress(static_cast<qint64>(pos) * 1000000);
}

void MainWindow::updatedSliderByTimer()
{
	qint64 cur_info, total_dur;
	total_dur = static_cast<qint64>(ms->ic->duration / 1000000LL); //long long变量，将视频总时长转换为秒
	cur_info = static_cast<qint64>(ms->audio_clock);
	QTime cur_time((cur_info / 3600) % 60, (cur_info / 60) % 60,
		cur_info % 60, (cur_info * 1000) % 1000);
	QTime total_time((total_dur / 3600) % 60, (total_dur / 60) % 60,
		total_dur % 60, (total_dur * 1000) % 1000);
	QString format = "mm:ss";
	if (total_dur > 3600)
		format = "hh:mm:ss";
	QString label_str(cur_time.toString(format) + " / " + total_time.toString(format));
	duration_label->setText(label_str);
	progress_slider->setValue(cur_info);
}

void MainWindow::sliderReleased()
{
	if (player->isStop())
		return;
	progress_timer->start();
}

