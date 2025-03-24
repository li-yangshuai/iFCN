#include<QApplication>
#include"MainWindow.h"

int main(int argc,char *argv[])
{
	QApplication app(argc, argv);

// 	app.setStyleSheet(

//     // 设置整体背景颜色为白色
//     "QMainWindow {"
//     "    background-color: #ffffff;"
//     "}"
    
//     // 工具栏样式
//     "QToolBar {"
//     "    background-color: #e0e0e0;"
//     "    border: 1px solid #d0d0d0;"
//     "    padding: 4px;"
//     "    spacing: 6px;"
//     "}"
//     "QToolBar QToolButton {"
//     "    background-color: #ffffff;"
//     "    border: 1px solid #d0d0d0;"
//     "    border-radius: 3px;"
//     "    padding: 5px;"
//     "    margin: 2px;"
//     "    icon-size: 20px 20px;"
//     "}"
//     "QToolBar QToolButton:hover {"
//     "    background-color: #d0e8d0;"
//     "    border-color: #a0c8a0;"
//     "}"
//     "QToolBar QToolButton:pressed {"
//     "    background-color: #a0c8a0;"
//     "    border-color: #80a080;"
//     "}"
    
//     // QComboBox 样式（例如时钟选择框）
//     "QComboBox {"
//     "    background-color: #ffffff;"
//     "    border: 1px solid #c0c0c0;"
//     "    padding: 2px 6px;"
//     "    min-width: 80px;"
//     "}"
//     "QComboBox QAbstractItemView {"
//     "    border: 1px solid #c0c0c0;"
//     "    selection-background-color: #d0e8d0;"
//     "    background-color: #ffffff;"
//     "}"
    
//     // QCheckBox 样式
//     "QCheckBox {"
//     "    spacing: 5px;"
//     "}"
//     "QCheckBox::indicator {"
//     "    width: 15px;"
//     "    height: 15px;"
//     "}"
//     "QCheckBox::indicator:checked {"
//     "    image: url(:/icons/checked.png);"  // 自定义图标可以用图像
//     "}"
    
//     // 按钮样式
//     "QPushButton {"
//     "    background-color: #4CAF50;"
//     "    color: white;"
//     "    border: 1px solid #4CAF50;"
//     "    padding: 5px 10px;"
//     "    border-radius: 4px;"
//     "    font-size: 14px;"
//     "}"
//     "QPushButton:hover {"
//     "    background-color: #45a049;"
//     "}"
//     "QPushButton:pressed {"
//     "    background-color: #388e3c;"
//     "}"
    
//     // QStatusBar 样式
//     "QStatusBar {"
//     "    background-color: #f0f0f0;"
//     "    border-top: 1px solid #d0d0d0;"
//     "    padding: 5px;"
//     "}"
    
//     // QTabWidget 样式（左侧工具箱）
//     "QTabWidget::pane {"
//     "    border: 1px solid #c0c0c0;"
//     "    background-color: #ffffff;"
//     "}"
//     "QTabBar::tab {"
//     "    background-color: #e0e0e0;"
//     "    border: 1px solid #c0c0c0;"
//     "    padding: 8px;"
//     "    min-width: 80px;"
//     "}"
//     "QTabBar::tab:selected {"
//     "    background-color: #4CAF50;"
//     "    color: white;"
//     "    border-bottom: 2px solid #4CAF50;"
//     "}"
    
//     // QToolBox 样式
//     "QToolBox::tab {"
//     "    background-color: #f0f0f0;"
//     "    border: 1px solid #d0d0d0;"
//     "    padding: 4px;"
//     "    margin: 2px;"
//     "    border-radius: 3px;"
//     "}"
//     "QToolBox::tab:selected {"
//     "    background-color: #4CAF50;"
//     "    color: white;"
//     "}"
    
//     // QLineEdit 样式（可输入路径的地方）
//     "QLineEdit {"
//     "    background-color: #ffffff;"
//     "    border: 1px solid #c0c0c0;"
//     "    padding: 4px;"
//     "    border-radius: 3px;"
//     "}"
    
//     // QLabel 样式（用于显示文本的标签）
//     "QLabel {"
//     "    color: #333333;"
//     "    font-size: 14px;"
//     "}"
    
//     // QGraphicsView 样式（主编辑区域）
//     "QGraphicsView {"
//     "    background-color: #fafafa;"
//     "    border: 1px solid #d0d0d0;"
//     "}"
// );


	MainWindow mainWindow;
	mainWindow.show();

	return app.exec();
}
