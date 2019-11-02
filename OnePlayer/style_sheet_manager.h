#ifndef STYLE_SHEET_MANAGER_H_
#define STYLE_SHEET_MANAGER_H_

#include <QObject>

class StyleSheetManager : public QObject
{
	Q_OBJECT

public:
	StyleSheetManager() = default;
	StyleSheetManager(const QString& filePath, QObject *parent = 0);
	~StyleSheetManager() = default;

	QString getfilePath() const;
	//1 加载指定的样式表字符串
	bool load(const QString& styleStr);
	//1.1加载指定qss文件
	bool loadFile(const QString& path);
	//2 重新加载指定的样式表文件
	bool reload();
	//3 切换指定的样式（换肤）
	bool switchTo(const QString& path);
	//4 qss文件路径存取器
	void setFilePath(const QString &path);

signals:
	void styleSheetLoaded();

private:
	QString qss_file_path;//qss文件路径
	
	QString readFile(const QString& path) const;//1 读取指定文件内容
	
};


#endif // !STYLE_SHEET_MANAGER_H_