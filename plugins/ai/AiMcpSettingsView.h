#pragma once

#include "AiMcpClient.h"
#include "plugins/PluginInterface.h"

#include <QDialog>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QPlainTextEdit;

namespace slotdeck::ui {
class SecretField;
}

namespace slotdeck::plugins::ai {

class AiPlugin;

class AiMcpServerDialog final : public QDialog {
    Q_OBJECT

  public:
    AiMcpServerDialog(PluginHost& host, McpServerDescriptor server, QStringList takenIdentifiers, QWidget* parent);

    [[nodiscard]] McpServerDescriptor server() const;

  private:
    void applyTransport();
    void accept() override;

    PluginHost& m_host;
    QStringList m_takenIdentifiers;
    QLineEdit* m_identifier{nullptr};
    QComboBox* m_transport{nullptr};
    QLineEdit* m_command{nullptr};
    QLineEdit* m_arguments{nullptr};
    QLineEdit* m_workdir{nullptr};
    QLineEdit* m_address{nullptr};
    ui::SecretField* m_apiKey{nullptr};
    QPlainTextEdit* m_roots{nullptr};
    QCheckBox* m_sampling{nullptr};
    QSpinBox* m_samplingTokens{nullptr};
    QLabel* m_validation{nullptr};
};

class AiMcpSettingsView final : public QWidget {
    Q_OBJECT

  public:
    AiMcpSettingsView(AiPlugin& plugin, PluginHost& host, QWidget* parent);

  private:
    void rebuild();
    void addServer();
    void editServer();
    void removeServer();
    void persist(const QVector<McpServerDescriptor>& servers);
    [[nodiscard]] int selectedRow() const;

    AiPlugin& m_plugin;
    PluginHost& m_host;
    QVector<McpServerDescriptor> m_servers;
    QTableWidget* m_grid{nullptr};
    QLabel* m_empty{nullptr};
};

} // namespace slotdeck::plugins::ai
