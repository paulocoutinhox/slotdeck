#pragma once

#include "plugins/PluginInterface.h"

namespace slotdeck::plugins::donate::translations {

inline TranslationEntries english() {
    return {{QStringLiteral("donate.plugin.title"), QStringLiteral("Donate")}, {QStringLiteral("donate.navigation.support"), QStringLiteral("Donate")}, {QStringLiteral("donate.view.title"), QStringLiteral("Support SlotDeck")}, {QStringLiteral("donate.view.name"), QStringLiteral("Paulo Coutinho")}, {QStringLiteral("donate.view.description"), QStringLiteral("SlotDeck is built independently. Your support helps fund continued development, maintenance, and new features.")}, {QStringLiteral("donate.view.github-sponsors"), QStringLiteral("GitHub Sponsors")}, {QStringLiteral("donate.view.kofi"), QStringLiteral("Support on Ko-fi")}, {QStringLiteral("donate.view.external-note"), QStringLiteral("Donation links open securely in your default browser.")}, {QStringLiteral("donate.error.open-title"), QStringLiteral("Donation link could not be opened")}, {QStringLiteral("donate.error.open-message"), QStringLiteral("The selected donation page could not be opened in the default browser")}};
}

inline TranslationEntries portuguese() {
    return {{QStringLiteral("donate.plugin.title"), QStringLiteral("Doações")}, {QStringLiteral("donate.navigation.support"), QStringLiteral("Doar")}, {QStringLiteral("donate.view.title"), QStringLiteral("Apoie o SlotDeck")}, {QStringLiteral("donate.view.name"), QStringLiteral("Paulo Coutinho")}, {QStringLiteral("donate.view.description"), QStringLiteral("O SlotDeck é desenvolvido de forma independente. Seu apoio ajuda a financiar o desenvolvimento contínuo, a manutenção e novos recursos.")}, {QStringLiteral("donate.view.github-sponsors"), QStringLiteral("GitHub Sponsors")}, {QStringLiteral("donate.view.kofi"), QStringLiteral("Apoiar pelo Ko-fi")}, {QStringLiteral("donate.view.external-note"), QStringLiteral("Os links de doação são abertos com segurança no navegador padrão.")}, {QStringLiteral("donate.error.open-title"), QStringLiteral("Não foi possível abrir o link de doação")}, {QStringLiteral("donate.error.open-message"), QStringLiteral("A página de doação selecionada não pôde ser aberta no navegador padrão")}};
}

inline TranslationCatalog catalog() {
    return {{QStringLiteral("en"), english()}, {QStringLiteral("pt"), portuguese()}};
}

} // namespace slotdeck::plugins::donate::translations
