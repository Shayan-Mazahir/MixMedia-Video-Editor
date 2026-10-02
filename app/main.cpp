#include "MainWindow.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

static void applyDarkTheme(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    const QColor window(0x1e, 0x1f, 0x22);
    const QColor base(0x17, 0x18, 0x1a);
    const QColor button(0x2b, 0x2d, 0x31);
    const QColor text(0xdc, 0xdd, 0xde);
    const QColor dimText(0x80, 0x82, 0x86);
    const QColor accent(0x2f, 0xc6, 0xb4);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, button);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::ToolTipBase, button);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, dimText);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::black);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::Disabled, QPalette::Text, dimText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, dimText);
    p.setColor(QPalette::Disabled, QPalette::WindowText, dimText);
    app.setPalette(p);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("MixMedia Video Editor");
    applyDarkTheme(app);

    // mixmedia [files...]            opens with those files imported
    // mixmedia --demo out.png files  puts them on the timeline, screenshots, quits
    QStringList args = app.arguments().mid(1);
    QString demoShot;
    if (args.size() >= 2 && args.first() == "--demo") {
        demoShot = args.at(1);
        args = args.mid(2);
    }

    MainWindow window;
    window.resize(1400, 850);
    window.show();

    if (!demoShot.isEmpty())
        window.runDemo(args, demoShot);
    else
        window.importFiles(args);

    return app.exec();
}
