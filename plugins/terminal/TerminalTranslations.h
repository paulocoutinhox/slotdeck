#pragma once

#include "plugins/PluginInterface.h"

namespace slotdeck::plugins::terminalplugin::translations {

inline TranslationEntries english() {
    return {{QStringLiteral("terminal.plugin.title"), QStringLiteral("Terminal")}, {QStringLiteral("terminal.navigation.workspace"), QStringLiteral("Terminal")}, {QStringLiteral("terminal.settings.general"), QStringLiteral("General")}, {QStringLiteral("terminal.settings.font-family"), QStringLiteral("Font family")}, {QStringLiteral("terminal.settings.font-size"), QStringLiteral("Font size")}, {QStringLiteral("terminal.settings.color-intensity"), QStringLiteral("Color intensity")}, {QStringLiteral("terminal.settings.confirm-paste"), QStringLiteral("Confirm multiline paste")}, {QStringLiteral("terminal.settings.allow-clipboard-write"), QStringLiteral("Allow programs to write to the clipboard")}, {QStringLiteral("terminal.actions.new-terminal"), QStringLiteral("New Terminal")}, {QStringLiteral("terminal.actions.new-tab"), QStringLiteral("New Tab")}, {QStringLiteral("terminal.actions.layout"), QStringLiteral("Layout")}, {QStringLiteral("terminal.actions.focus"), QStringLiteral("Focus terminal")}, {QStringLiteral("terminal.actions.restore-layout"), QStringLiteral("Restore layout")}, {QStringLiteral("terminal.actions.menu"), QStringLiteral("Terminal actions")}, {QStringLiteral("terminal.actions.close-terminal"), QStringLiteral("Close Terminal")}, {QStringLiteral("terminal.actions.restart"), QStringLiteral("Restart")}, {QStringLiteral("terminal.actions.open-in-editor"), QStringLiteral("Open this directory in the Code Editor")}, {QStringLiteral("terminal.actions.serve-directory"), QStringLiteral("Serve this directory as a Web Server")}, {QStringLiteral("terminal.actions.restart-shell"), QStringLiteral("Restart Shell")}, {QStringLiteral("terminal.actions.move-to-shelf"), QStringLiteral("Move to Shelf")}, {QStringLiteral("terminal.tabs.close-title"), QStringLiteral("Close Tab")}, {QStringLiteral("terminal.tabs.close-message"), QStringLiteral("Close “%1”?")}, {QStringLiteral("terminal.tabs.close-detail"), QStringLiteral("Every terminal session in this tab will be terminated.")}, {QStringLiteral("terminal.tabs.close-action"), QStringLiteral("Close Tab")}, {QStringLiteral("terminal.tabs.rename-title"), QStringLiteral("Rename Tab")}, {QStringLiteral("terminal.tabs.name"), QStringLiteral("Name")}, {QStringLiteral("terminal.session.close-title"), QStringLiteral("Close Terminal")}, {QStringLiteral("terminal.session.close-message"), QStringLiteral("Close “%1”?")}, {QStringLiteral("terminal.session.close-detail"), QStringLiteral("Its running shell or process will be terminated.")}, {QStringLiteral("terminal.session.close-action"), QStringLiteral("Close Terminal")}, {QStringLiteral("terminal.session.rename-title"), QStringLiteral("Rename Terminal")}, {QStringLiteral("terminal.session.name"), QStringLiteral("Name")}, {QStringLiteral("terminal.session.process-exited"), QStringLiteral("Process exited with code %1")}, {QStringLiteral("terminal.session.bell"), QStringLiteral("A program rang the terminal bell")}, {QStringLiteral("terminal.find.label"), QStringLiteral("Find in terminal")}, {QStringLiteral("terminal.find.case-sensitive"), QStringLiteral("Match case")}, {QStringLiteral("terminal.find.whole-word"), QStringLiteral("Whole words only")}, {QStringLiteral("terminal.find.previous"), QStringLiteral("Previous match")}, {QStringLiteral("terminal.find.next"), QStringLiteral("Next match")}, {QStringLiteral("terminal.find.close"), QStringLiteral("Close the find bar")}, {QStringLiteral("terminal.find.not-found"), QStringLiteral("No matches")}, {QStringLiteral("terminal.menu.copy"), QStringLiteral("Copy")}, {QStringLiteral("terminal.menu.paste"), QStringLiteral("Paste")}, {QStringLiteral("terminal.menu.select-all"), QStringLiteral("Select All")}, {QStringLiteral("terminal.menu.clear"), QStringLiteral("Clear Buffer")}, {QStringLiteral("terminal.paste.confirm-title"), QStringLiteral("Confirm Paste")}, {QStringLiteral("terminal.paste.confirm-message"), QStringLiteral("Paste %1 lines into this terminal?")}, {QStringLiteral("terminal.paste.confirm-detail"), QStringLiteral("Multiline text may execute more than one command.")}, {QStringLiteral("terminal.paste.confirm-action"), QStringLiteral("Paste")}, {QStringLiteral("terminal.shelf.outside-active-layout"), QStringLiteral("Sessions outside the active layout")}, {QStringLiteral("terminal.shelf.accessible-name"), QStringLiteral("Shelved terminal %1")}, {QStringLiteral("terminal.shelf.instructions"), QStringLiteral("Drag to a layout slot or click to show in the active slot")}, {QStringLiteral("terminal.slot.create"), QStringLiteral("Create a terminal in this slot")}, {QStringLiteral("terminal.slot.drop-session"), QStringLiteral("Drop a session here")}, {QStringLiteral("terminal.workspace.default-name"), QStringLiteral("Workspace")}, {QStringLiteral("terminal.workspace.numbered"), QStringLiteral("Workspace %1")}, {QStringLiteral("terminal.status.workspace-single"), QStringLiteral("1 workspace")}, {QStringLiteral("terminal.status.workspace-multiple"), QStringLiteral("%1 workspaces")}, {QStringLiteral("terminal.status.single"), QStringLiteral("1 terminal")}, {QStringLiteral("terminal.status.multiple"), QStringLiteral("%1 terminals")}, {QStringLiteral("terminal.layout.slots"), QStringLiteral("%1 · %2 slots")}, {QStringLiteral("terminal.error.settings-save-message"), QStringLiteral("Terminal settings could not be saved")}, {QStringLiteral("terminal.error.interaction"), QStringLiteral("Terminal interaction failed")}, {QStringLiteral("terminal.error.start-title"), QStringLiteral("Terminal could not start")}, {QStringLiteral("terminal.error.layout-title"), QStringLiteral("Invalid layout")}, {QStringLiteral("terminal.error.layout-message"), QStringLiteral("The selected layout does not exist")}, {QStringLiteral("terminal.error.slot-title"), QStringLiteral("Invalid slot")}, {QStringLiteral("terminal.error.slot-message"), QStringLiteral("The selected slot is not available")}, {QStringLiteral("terminal.error.runtime-title"), QStringLiteral("Terminal error")}, {QStringLiteral("terminal.error.save-title"), QStringLiteral("Workspace is not saved")}};
}

inline TranslationEntries portuguese() {
    TranslationEntries entries = english();
    entries[QStringLiteral("terminal.plugin.title")] = QStringLiteral("Terminal");
    entries[QStringLiteral("terminal.navigation.workspace")] = QStringLiteral("Terminal");
    entries[QStringLiteral("terminal.settings.general")] = QStringLiteral("Geral");
    entries[QStringLiteral("terminal.settings.font-family")] = QStringLiteral("Família da fonte");
    entries[QStringLiteral("terminal.settings.font-size")] = QStringLiteral("Tamanho da fonte");
    entries[QStringLiteral("terminal.settings.color-intensity")] = QStringLiteral("Intensidade das cores");
    entries[QStringLiteral("terminal.settings.confirm-paste")] = QStringLiteral("Confirmar colagem com várias linhas");
    entries[QStringLiteral("terminal.actions.new-terminal")] = QStringLiteral("Novo Terminal");
    entries[QStringLiteral("terminal.actions.new-tab")] = QStringLiteral("Nova Aba");
    entries[QStringLiteral("terminal.actions.layout")] = QStringLiteral("Layout");
    entries[QStringLiteral("terminal.actions.focus")] = QStringLiteral("Focar terminal");
    entries[QStringLiteral("terminal.actions.restore-layout")] = QStringLiteral("Restaurar layout");
    entries[QStringLiteral("terminal.actions.menu")] = QStringLiteral("Ações do terminal");
    entries[QStringLiteral("terminal.actions.close-terminal")] = QStringLiteral("Fechar Terminal");
    entries[QStringLiteral("terminal.actions.open-in-editor")] = QStringLiteral("Abrir este diretório no Editor de Código");
    entries[QStringLiteral("terminal.actions.serve-directory")] = QStringLiteral("Servir este diretório como Web Server");
    entries[QStringLiteral("terminal.actions.restart")] = QStringLiteral("Reiniciar");
    entries[QStringLiteral("terminal.actions.restart-shell")] = QStringLiteral("Reiniciar Shell");
    entries[QStringLiteral("terminal.actions.move-to-shelf")] = QStringLiteral("Mover para a Estante");
    entries[QStringLiteral("terminal.tabs.close-title")] = QStringLiteral("Fechar Aba");
    entries[QStringLiteral("terminal.tabs.close-message")] = QStringLiteral("Fechar “%1”?");
    entries[QStringLiteral("terminal.tabs.close-detail")] = QStringLiteral("Todas as sessões de terminal desta aba serão encerradas.");
    entries[QStringLiteral("terminal.tabs.close-action")] = QStringLiteral("Fechar Aba");
    entries[QStringLiteral("terminal.tabs.rename-title")] = QStringLiteral("Renomear Aba");
    entries[QStringLiteral("terminal.tabs.name")] = QStringLiteral("Nome");
    entries[QStringLiteral("terminal.session.close-title")] = QStringLiteral("Fechar Terminal");
    entries[QStringLiteral("terminal.session.close-message")] = QStringLiteral("Fechar “%1”?");
    entries[QStringLiteral("terminal.session.close-detail")] = QStringLiteral("O shell ou processo em execução será encerrado.");
    entries[QStringLiteral("terminal.session.close-action")] = QStringLiteral("Fechar Terminal");
    entries[QStringLiteral("terminal.session.rename-title")] = QStringLiteral("Renomear Terminal");
    entries[QStringLiteral("terminal.session.name")] = QStringLiteral("Nome");
    entries[QStringLiteral("terminal.session.process-exited")] = QStringLiteral("O processo encerrou com o código %1");
    entries[QStringLiteral("terminal.find.label")] = QStringLiteral("Procurar no terminal");
    entries[QStringLiteral("terminal.find.case-sensitive")] = QStringLiteral("Diferenciar maiúsculas");
    entries[QStringLiteral("terminal.find.whole-word")] = QStringLiteral("Apenas palavras inteiras");
    entries[QStringLiteral("terminal.find.previous")] = QStringLiteral("Ocorrência anterior");
    entries[QStringLiteral("terminal.find.next")] = QStringLiteral("Próxima ocorrência");
    entries[QStringLiteral("terminal.find.close")] = QStringLiteral("Fechar a barra de busca");
    entries[QStringLiteral("terminal.find.not-found")] = QStringLiteral("Nenhuma ocorrência");
    entries[QStringLiteral("terminal.settings.allow-clipboard-write")] = QStringLiteral("Permitir que programas escrevam na área de transferência");
    entries[QStringLiteral("terminal.session.bell")] = QStringLiteral("Um programa tocou o alerta do terminal");
    entries[QStringLiteral("terminal.menu.copy")] = QStringLiteral("Copiar");
    entries[QStringLiteral("terminal.menu.paste")] = QStringLiteral("Colar");
    entries[QStringLiteral("terminal.menu.select-all")] = QStringLiteral("Selecionar Tudo");
    entries[QStringLiteral("terminal.menu.clear")] = QStringLiteral("Limpar Buffer");
    entries[QStringLiteral("terminal.paste.confirm-title")] = QStringLiteral("Confirmar Colagem");
    entries[QStringLiteral("terminal.paste.confirm-message")] = QStringLiteral("Colar %1 linhas neste terminal?");
    entries[QStringLiteral("terminal.paste.confirm-detail")] = QStringLiteral("O texto com várias linhas pode executar mais de um comando.");
    entries[QStringLiteral("terminal.paste.confirm-action")] = QStringLiteral("Colar");
    entries[QStringLiteral("terminal.shelf.outside-active-layout")] = QStringLiteral("Sessões fora do layout ativo");
    entries[QStringLiteral("terminal.shelf.accessible-name")] = QStringLiteral("Terminal %1 na estante");
    entries[QStringLiteral("terminal.shelf.instructions")] = QStringLiteral("Arraste para um espaço do layout ou clique para exibir no espaço ativo");
    entries[QStringLiteral("terminal.slot.create")] = QStringLiteral("Criar um terminal neste espaço");
    entries[QStringLiteral("terminal.slot.drop-session")] = QStringLiteral("Solte uma sessão aqui");
    entries[QStringLiteral("terminal.workspace.default-name")] = QStringLiteral("Espaço de Trabalho");
    entries[QStringLiteral("terminal.workspace.numbered")] = QStringLiteral("Espaço de Trabalho %1");
    entries[QStringLiteral("terminal.status.workspace-single")] = QStringLiteral("1 espaço de trabalho");
    entries[QStringLiteral("terminal.status.workspace-multiple")] = QStringLiteral("%1 espaços de trabalho");
    entries[QStringLiteral("terminal.status.single")] = QStringLiteral("1 terminal");
    entries[QStringLiteral("terminal.status.multiple")] = QStringLiteral("%1 terminais");
    entries[QStringLiteral("terminal.layout.slots")] = QStringLiteral("%1 · %2 espaços");
    entries[QStringLiteral("terminal.error.settings-save-message")] = QStringLiteral("Não foi possível salvar as configurações do terminal");
    entries[QStringLiteral("terminal.error.interaction")] = QStringLiteral("Falha na interação com o terminal");
    entries[QStringLiteral("terminal.error.start-title")] = QStringLiteral("Não foi possível iniciar o terminal");
    entries[QStringLiteral("terminal.error.layout-title")] = QStringLiteral("Layout inválido");
    entries[QStringLiteral("terminal.error.layout-message")] = QStringLiteral("O layout selecionado não existe");
    entries[QStringLiteral("terminal.error.slot-title")] = QStringLiteral("Espaço inválido");
    entries[QStringLiteral("terminal.error.slot-message")] = QStringLiteral("O espaço selecionado não está disponível");
    entries[QStringLiteral("terminal.error.runtime-title")] = QStringLiteral("Erro no terminal");
    entries[QStringLiteral("terminal.error.save-title")] = QStringLiteral("A área de trabalho não foi salva");
    return entries;
}

inline TranslationCatalog catalog() {
    return {{QStringLiteral("en"), english()}, {QStringLiteral("pt"), portuguese()}};
}

} // namespace slotdeck::plugins::terminalplugin::translations
