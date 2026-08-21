#pragma once

#include "plugins/PluginInterface.h"

namespace slotdeck::plugins::browser::translations {

inline TranslationEntries english() {
    return {{QStringLiteral("browser.plugin.title"), QStringLiteral("Browser")}, {QStringLiteral("browser.navigation.web"), QStringLiteral("Browser")}, {QStringLiteral("browser.settings.general"), QStringLiteral("General")}, {QStringLiteral("browser.settings.homepage"), QStringLiteral("Homepage")}, {QStringLiteral("browser.actions.new-tab"), QStringLiteral("New Tab")}, {QStringLiteral("browser.actions.close-tab"), QStringLiteral("Close Tab")}, {QStringLiteral("browser.actions.back"), QStringLiteral("Back")}, {QStringLiteral("browser.actions.forward"), QStringLiteral("Forward")}, {QStringLiteral("browser.actions.reload"), QStringLiteral("Reload")}, {QStringLiteral("browser.actions.stop"), QStringLiteral("Stop")}, {QStringLiteral("browser.actions.home"), QStringLiteral("Home")}, {QStringLiteral("browser.actions.cancel"), QStringLiteral("Cancel")}, {QStringLiteral("browser.actions.save"), QStringLiteral("Save")}, {QStringLiteral("browser.actions.edit"), QStringLiteral("Edit")}, {QStringLiteral("browser.actions.remove"), QStringLiteral("Remove")}, {QStringLiteral("browser.address.placeholder"), QStringLiteral("Enter a web address")}, {QStringLiteral("browser.tabs.new"), QStringLiteral("New Tab")}, {QStringLiteral("browser.tabs.empty"), QStringLiteral("Every tab is closed")}, {QStringLiteral("browser.bookmarks.title"), QStringLiteral("Bookmarks")}, {QStringLiteral("browser.bookmarks.toggle"), QStringLiteral("Show Bookmarks")}, {QStringLiteral("browser.bookmarks.add-group"), QStringLiteral("Add Group")}, {QStringLiteral("browser.bookmarks.edit-group"), QStringLiteral("Edit Group")}, {QStringLiteral("browser.bookmarks.add-bookmark"), QStringLiteral("Add Bookmark")}, {QStringLiteral("browser.bookmarks.edit-bookmark"), QStringLiteral("Edit Bookmark")}, {QStringLiteral("browser.bookmarks.name"), QStringLiteral("Name")}, {QStringLiteral("browser.bookmarks.address"), QStringLiteral("Web address")}, {QStringLiteral("browser.bookmarks.group"), QStringLiteral("Group")}, {QStringLiteral("browser.bookmarks.ungrouped"), QStringLiteral("Ungrouped")}, {QStringLiteral("browser.bookmarks.open-current"), QStringLiteral("Open Here")}, {QStringLiteral("browser.bookmarks.open-new"), QStringLiteral("Open in New Tab")}, {QStringLiteral("browser.bookmarks.remove-bookmark"), QStringLiteral("Remove Bookmark")}, {QStringLiteral("browser.bookmarks.remove-bookmark-question"), QStringLiteral("Remove this bookmark from your saved list?")}, {QStringLiteral("browser.bookmarks.remove-group"), QStringLiteral("Remove Group")}, {QStringLiteral("browser.bookmarks.remove-group-question"), QStringLiteral("Remove this group? Its bookmarks will become ungrouped")}, {QStringLiteral("browser.error.invalid-address"), QStringLiteral("The web address is invalid")}, {QStringLiteral("browser.error.invalid-bookmark"), QStringLiteral("The bookmark information is invalid")}, {QStringLiteral("browser.error.operation"), QStringLiteral("The browser operation could not be completed")}, {QStringLiteral("browser.error.persistence"), QStringLiteral("The browser session could not be saved")}};
}

inline TranslationEntries portuguese() {
    TranslationEntries entries = english();
    entries[QStringLiteral("browser.plugin.title")] = QStringLiteral("Navegador");
    entries[QStringLiteral("browser.navigation.web")] = QStringLiteral("Navegador");
    entries[QStringLiteral("browser.settings.general")] = QStringLiteral("Geral");
    entries[QStringLiteral("browser.settings.homepage")] = QStringLiteral("Página inicial");
    entries[QStringLiteral("browser.actions.new-tab")] = QStringLiteral("Nova aba");
    entries[QStringLiteral("browser.actions.close-tab")] = QStringLiteral("Fechar aba");
    entries[QStringLiteral("browser.actions.back")] = QStringLiteral("Voltar");
    entries[QStringLiteral("browser.actions.forward")] = QStringLiteral("Avançar");
    entries[QStringLiteral("browser.actions.reload")] = QStringLiteral("Recarregar");
    entries[QStringLiteral("browser.actions.stop")] = QStringLiteral("Parar");
    entries[QStringLiteral("browser.actions.home")] = QStringLiteral("Início");
    entries[QStringLiteral("browser.actions.cancel")] = QStringLiteral("Cancelar");
    entries[QStringLiteral("browser.actions.save")] = QStringLiteral("Salvar");
    entries[QStringLiteral("browser.actions.edit")] = QStringLiteral("Editar");
    entries[QStringLiteral("browser.actions.remove")] = QStringLiteral("Remover");
    entries[QStringLiteral("browser.address.placeholder")] = QStringLiteral("Digite um endereço da web");
    entries[QStringLiteral("browser.tabs.new")] = QStringLiteral("Nova aba");
    entries[QStringLiteral("browser.tabs.empty")] = QStringLiteral("Todas as abas estão fechadas");
    entries[QStringLiteral("browser.bookmarks.title")] = QStringLiteral("Favoritos");
    entries[QStringLiteral("browser.bookmarks.toggle")] = QStringLiteral("Mostrar favoritos");
    entries[QStringLiteral("browser.bookmarks.add-group")] = QStringLiteral("Adicionar grupo");
    entries[QStringLiteral("browser.bookmarks.edit-group")] = QStringLiteral("Editar grupo");
    entries[QStringLiteral("browser.bookmarks.add-bookmark")] = QStringLiteral("Adicionar favorito");
    entries[QStringLiteral("browser.bookmarks.edit-bookmark")] = QStringLiteral("Editar favorito");
    entries[QStringLiteral("browser.bookmarks.name")] = QStringLiteral("Nome");
    entries[QStringLiteral("browser.bookmarks.address")] = QStringLiteral("Endereço da web");
    entries[QStringLiteral("browser.bookmarks.group")] = QStringLiteral("Grupo");
    entries[QStringLiteral("browser.bookmarks.ungrouped")] = QStringLiteral("Sem grupo");
    entries[QStringLiteral("browser.bookmarks.open-current")] = QStringLiteral("Abrir aqui");
    entries[QStringLiteral("browser.bookmarks.open-new")] = QStringLiteral("Abrir em nova aba");
    entries[QStringLiteral("browser.bookmarks.remove-bookmark")] = QStringLiteral("Remover favorito");
    entries[QStringLiteral("browser.bookmarks.remove-bookmark-question")] = QStringLiteral("Remover este favorito da sua lista salva?");
    entries[QStringLiteral("browser.bookmarks.remove-group")] = QStringLiteral("Remover grupo");
    entries[QStringLiteral("browser.bookmarks.remove-group-question")] = QStringLiteral("Remover este grupo? Seus favoritos ficarão sem grupo");
    entries[QStringLiteral("browser.error.invalid-address")] = QStringLiteral("O endereço da web é inválido");
    entries[QStringLiteral("browser.error.invalid-bookmark")] = QStringLiteral("As informações do favorito são inválidas");
    entries[QStringLiteral("browser.error.operation")] = QStringLiteral("A operação do navegador não pôde ser concluída");
    entries[QStringLiteral("browser.error.persistence")] = QStringLiteral("A sessão do navegador não pôde ser salva");
    return entries;
}

inline TranslationCatalog catalog() {
    return {{QStringLiteral("en"), english()}, {QStringLiteral("pt"), portuguese()}};
}

} // namespace slotdeck::plugins::browser::translations
