
#pragma once

#include <QTreeWidget>


class QTreeWidgetItem;

struct DpsoCfg;


class LangBrowser : public QTreeWidget {
    Q_OBJECT

public:
    explicit LangBrowser(QWidget* parent = nullptr);

    void loadState(const DpsoCfg* cfg);
    void saveState(DpsoCfg* cfg) const;
private slots:
    void toggleLang(QTreeWidgetItem* item, int column);
};
