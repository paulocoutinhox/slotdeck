#pragma once

#include "plugins/PluginInterface.h"

namespace slotdeck::plugins::codeeditor::translations {

inline TranslationEntries english() {
    return {{QStringLiteral("code-editor.plugin.title"), QStringLiteral("Code Editor")}, {QStringLiteral("code-editor.find.case-sensitive"), QStringLiteral("Match case")}, {QStringLiteral("code-editor.find.whole-word"), QStringLiteral("Whole word")}, {QStringLiteral("code-editor.error.encoding-unsupported"), QStringLiteral("The file uses an encoding this editor cannot write back:")}, {QStringLiteral("code-editor.problems.title"), QStringLiteral("Problems")}, {QStringLiteral("code-editor.references.title"), QStringLiteral("References")}, {QStringLiteral("code-editor.references.context"), QStringLiteral("Location")}, {QStringLiteral("code-editor.symbols.search"), QStringLiteral("Search symbols in the workspace")}, {QStringLiteral("code-editor.actions.go-to-definition"), QStringLiteral("Go to Definition")}, {QStringLiteral("code-editor.actions.go-to-declaration"), QStringLiteral("Go to Declaration")}, {QStringLiteral("code-editor.actions.go-to-type-definition"), QStringLiteral("Go to Type Definition")}, {QStringLiteral("code-editor.actions.go-to-implementation"), QStringLiteral("Go to Implementation")}, {QStringLiteral("code-editor.actions.find-references"), QStringLiteral("Find References")}, {QStringLiteral("code-editor.view.empty"), QStringLiteral("Open a folder to start editing")}, {QStringLiteral("code-editor.settings.font-family"), QStringLiteral("Editor font")}, {QStringLiteral("code-editor.settings.font-family-system"), QStringLiteral("System monospace")}, {QStringLiteral("code-editor.settings.font-size"), QStringLiteral("Font size")}, {QStringLiteral("code-editor.navigation.editor"), QStringLiteral("Code Editor")}, {QStringLiteral("code-editor.settings.appearance"), QStringLiteral("Appearance")}, {QStringLiteral("code-editor.settings.language-servers"), QStringLiteral("Language servers")}, {QStringLiteral("code-editor.settings.language-servers-enabled"), QStringLiteral("Enable language servers")}, {QStringLiteral("code-editor.settings.language-servers-enabled-description"), QStringLiteral("Starts language servers for open documents and shows the workspace problems panel")}, {QStringLiteral("code-editor.settings.language"), QStringLiteral("Language")}, {QStringLiteral("code-editor.settings.executable"), QStringLiteral("Executable")}, {QStringLiteral("code-editor.settings.not-found"), QStringLiteral("Not found")}, {QStringLiteral("code-editor.actions.refresh"), QStringLiteral("Refresh")}, {QStringLiteral("code-editor.actions.open-folder"), QStringLiteral("Open Folder")}, {QStringLiteral("code-editor.actions.save-all"), QStringLiteral("Save All")}, {QStringLiteral("code-editor.actions.new-file"), QStringLiteral("New File")}, {QStringLiteral("code-editor.actions.new-folder"), QStringLiteral("New Folder")}, {QStringLiteral("code-editor.actions.rename"), QStringLiteral("Rename")}, {QStringLiteral("code-editor.actions.move"), QStringLiteral("Move")}, {QStringLiteral("code-editor.actions.delete"), QStringLiteral("Delete")}, {QStringLiteral("code-editor.actions.name"), QStringLiteral("Name")}, {QStringLiteral("code-editor.find.label"), QStringLiteral("Text")}, {QStringLiteral("code-editor.find.not-found"), QStringLiteral("The text was not found in the current file")}, {QStringLiteral("code-editor.problems.file"), QStringLiteral("File")}, {QStringLiteral("code-editor.problems.line"), QStringLiteral("Line")}, {QStringLiteral("code-editor.problems.message"), QStringLiteral("Problem")}, {QStringLiteral("code-editor.error.title"), QStringLiteral("Code Editor")}, {QStringLiteral("code-editor.error.operation"), QStringLiteral("The code editor operation failed")}, {QStringLiteral("code-editor.error.path-outside"), QStringLiteral("The selected path is outside the open workspace")}, {QStringLiteral("code-editor.error.workspace-unavailable"), QStringLiteral("The saved workspace folder is unavailable")}, {QStringLiteral("code-editor.error.binary-file"), QStringLiteral("Binary files cannot be edited as text")}, {QStringLiteral("code-editor.error.encoding"), QStringLiteral("The file is not valid UTF-8 text")}, {QStringLiteral("code-editor.error.external-conflict"), QStringLiteral("The file changed outside SlotDeck while it has unsaved changes. The editor preserved the unsaved buffer")}, {QStringLiteral("code-editor.error.external-removed"), QStringLiteral("The file was removed outside SlotDeck while it has unsaved changes. The editor preserved the unsaved buffer")}, {QStringLiteral("code-editor.close.title"), QStringLiteral("Discard Unsaved Changes")}, {QStringLiteral("code-editor.close.file-message"), QStringLiteral("Close this file and discard its unsaved changes?")}, {QStringLiteral("code-editor.close.workspace-message"), QStringLiteral("Close this folder and discard unsaved changes in its open files?")}, {QStringLiteral("code-editor.close.discard"), QStringLiteral("Discard Changes")}, {QStringLiteral("code-editor.delete.title"), QStringLiteral("Delete Permanently")}, {QStringLiteral("code-editor.delete.file-message"), QStringLiteral("Permanently delete this file?")}, {QStringLiteral("code-editor.delete.folder-message"), QStringLiteral("Permanently delete this folder and all of its contents?")}, {QStringLiteral("code-editor.settings.word-wrap"), QStringLiteral("Word wrap")}, {QStringLiteral("code-editor.settings.word-wrap-description"), QStringLiteral("Wrap long lines at the editor width instead of scrolling horizontally")}, {QStringLiteral("code-editor.find.previous"), QStringLiteral("Previous match")}, {QStringLiteral("code-editor.find.next"), QStringLiteral("Next match")}, {QStringLiteral("code-editor.find.close"), QStringLiteral("Close the find bar")}, {QStringLiteral("code-editor.status.cursor"), QStringLiteral("Ln %1, Col %2")}, {QStringLiteral("code-editor.status.space-size"), QStringLiteral("Spaces: %1")}, {QStringLiteral("code-editor.status.tab-size"), QStringLiteral("Tab size: %1")}, {QStringLiteral("code-editor.status.word-wrap"), QStringLiteral("Word wrap")}, {QStringLiteral("code-editor.error.editorconfig-charset"), QStringLiteral("The .editorconfig file declares a character set that the editor does not support")}};
}

inline TranslationEntries portuguese() {
    TranslationEntries values = english();
    values[QStringLiteral("code-editor.plugin.title")] = QStringLiteral("Editor de Código");
    values[QStringLiteral("code-editor.error.workspace-open")] = QStringLiteral("Essa pasta não pôde ser aberta como espaço de trabalho");
    values[QStringLiteral("code-editor.error.charset-unrepresentable")] = QStringLiteral("Este arquivo tem caracteres que %1 não consegue escrever, então não foi salvo");
    values[QStringLiteral("code-editor.settings.files")] = QStringLiteral("Arquivos");
    values[QStringLiteral("code-editor.settings.default-charset")] = QStringLiteral("Codificação quando o arquivo não é UTF-8");
    values[QStringLiteral("code-editor.settings.default-charset-description")] = QStringLiteral("Usada quando o arquivo não tem marca de ordem de bytes e não é UTF-8 válido");
    values[QStringLiteral("code-editor.status.encoding-action")] = QStringLiteral("Ler ou gravar este arquivo em outra codificação");
    values[QStringLiteral("code-editor.status.reopen-with-encoding")] = QStringLiteral("Reabrir com codificação");
    values[QStringLiteral("code-editor.status.save-with-encoding")] = QStringLiteral("Salvar com codificação");
    values[QStringLiteral("code-editor.status.reopen-title")] = QStringLiteral("Ler este arquivo de novo?");
    values[QStringLiteral("code-editor.status.reopen-message")] = QStringLiteral("Ler o arquivo de novo substitui o que você ainda não salvou.");
    values[QStringLiteral("code-editor.status.reopen-action")] = QStringLiteral("Ler de novo");
    values[QStringLiteral("code-editor.finder.title")] = QStringLiteral("Localizar arquivo");
    values[QStringLiteral("code-editor.finder.placeholder")] = QStringLiteral("Digite parte do nome");
    values[QStringLiteral("code-editor.finder.count")] = QStringLiteral("%1 arquivos");
    values[QStringLiteral("code-editor.finder.count-capped")] = QStringLiteral("Os primeiros %1 arquivos desta pasta");
    values[QStringLiteral("code-editor.tree.filter")] = QStringLiteral("Filtrar arquivos");
    values[QStringLiteral("code-editor.tree.filter-placeholder")] = QStringLiteral("Parte de um nome");
    values[QStringLiteral("code-editor.problems.filter")] = QStringLiteral("Filtrar por arquivo ou mensagem");
    values[QStringLiteral("code-editor.navigation.editor")] = QStringLiteral("Editor de Código");
    values[QStringLiteral("code-editor.settings.appearance")] = QStringLiteral("Aparência");
    values[QStringLiteral("code-editor.settings.language-servers")] = QStringLiteral("Servidores de linguagem");
    values[QStringLiteral("code-editor.settings.language-servers-enabled")] = QStringLiteral("Ativar servidores de linguagem");
    values[QStringLiteral("code-editor.settings.language-servers-enabled-description")] = QStringLiteral("Inicia servidores de linguagem para os documentos abertos e mostra o painel de problemas do espaço de trabalho");
    values[QStringLiteral("code-editor.settings.language")] = QStringLiteral("Linguagem");
    values[QStringLiteral("code-editor.settings.executable")] = QStringLiteral("Executável");
    values[QStringLiteral("code-editor.settings.not-found")] = QStringLiteral("Não encontrado");
    values[QStringLiteral("code-editor.actions.refresh")] = QStringLiteral("Atualizar");
    values[QStringLiteral("code-editor.actions.open-folder")] = QStringLiteral("Abrir Pasta");
    values[QStringLiteral("code-editor.view.empty")] = QStringLiteral("Abra uma pasta para começar a editar");
    values[QStringLiteral("code-editor.actions.save-all")] = QStringLiteral("Salvar Tudo");
    values[QStringLiteral("code-editor.actions.new-file")] = QStringLiteral("Novo Arquivo");
    values[QStringLiteral("code-editor.actions.new-folder")] = QStringLiteral("Nova Pasta");
    values[QStringLiteral("code-editor.actions.rename")] = QStringLiteral("Renomear");
    values[QStringLiteral("code-editor.actions.move")] = QStringLiteral("Mover");
    values[QStringLiteral("code-editor.actions.delete")] = QStringLiteral("Excluir");
    values[QStringLiteral("code-editor.actions.name")] = QStringLiteral("Nome");
    values[QStringLiteral("code-editor.settings.font-family")] = QStringLiteral("Fonte do editor");
    values[QStringLiteral("code-editor.settings.font-family-system")] = QStringLiteral("Monoespaçada do sistema");
    values[QStringLiteral("code-editor.settings.font-size")] = QStringLiteral("Tamanho da fonte");
    values[QStringLiteral("code-editor.settings.word-wrap")] = QStringLiteral("Quebra de linha");
    values[QStringLiteral("code-editor.settings.word-wrap-description")] = QStringLiteral("Quebra as linhas longas na largura do editor em vez de rolar horizontalmente");
    values[QStringLiteral("code-editor.find.previous")] = QStringLiteral("Ocorrência anterior");
    values[QStringLiteral("code-editor.find.next")] = QStringLiteral("Próxima ocorrência");
    values[QStringLiteral("code-editor.find.close")] = QStringLiteral("Fechar a barra de busca");
    values[QStringLiteral("code-editor.status.cursor")] = QStringLiteral("Lin %1, Col %2");
    values[QStringLiteral("code-editor.status.space-size")] = QStringLiteral("Espaços: %1");
    values[QStringLiteral("code-editor.status.tab-size")] = QStringLiteral("Tamanho da tabulação: %1");
    values[QStringLiteral("code-editor.status.word-wrap")] = QStringLiteral("Quebra de linha");
    values[QStringLiteral("code-editor.error.editorconfig-charset")] = QStringLiteral("O arquivo .editorconfig declara um conjunto de caracteres que o editor não suporta");
    values[QStringLiteral("code-editor.find.label")] = QStringLiteral("Texto");
    values[QStringLiteral("code-editor.find.not-found")] = QStringLiteral("O texto não foi encontrado no arquivo atual");
    values[QStringLiteral("code-editor.error.encoding-unsupported")] = QStringLiteral("O arquivo usa uma codificação que este editor não sabe gravar de volta:");
    values[QStringLiteral("code-editor.find.case-sensitive")] = QStringLiteral("Diferenciar maiúsculas");
    values[QStringLiteral("code-editor.find.whole-word")] = QStringLiteral("Palavra inteira");
    values[QStringLiteral("code-editor.problems.title")] = QStringLiteral("Problemas");
    values[QStringLiteral("code-editor.references.title")] = QStringLiteral("Referências");
    values[QStringLiteral("code-editor.references.context")] = QStringLiteral("Local");
    values[QStringLiteral("code-editor.symbols.search")] = QStringLiteral("Buscar símbolos no espaço de trabalho");
    values[QStringLiteral("code-editor.actions.go-to-definition")] = QStringLiteral("Ir para a definição");
    values[QStringLiteral("code-editor.actions.go-to-declaration")] = QStringLiteral("Ir para a declaração");
    values[QStringLiteral("code-editor.actions.go-to-type-definition")] = QStringLiteral("Ir para a definição do tipo");
    values[QStringLiteral("code-editor.actions.go-to-implementation")] = QStringLiteral("Ir para a implementação");
    values[QStringLiteral("code-editor.actions.find-references")] = QStringLiteral("Localizar referências");
    values[QStringLiteral("code-editor.problems.file")] = QStringLiteral("Arquivo");
    values[QStringLiteral("code-editor.problems.line")] = QStringLiteral("Linha");
    values[QStringLiteral("code-editor.problems.message")] = QStringLiteral("Problema");
    values[QStringLiteral("code-editor.error.title")] = QStringLiteral("Editor de Código");
    values[QStringLiteral("code-editor.error.operation")] = QStringLiteral("A operação do editor de código falhou");
    values[QStringLiteral("code-editor.error.path-outside")] = QStringLiteral("O caminho selecionado está fora da pasta aberta");
    values[QStringLiteral("code-editor.error.workspace-unavailable")] = QStringLiteral("A pasta salva do espaço de trabalho não está disponível");
    values[QStringLiteral("code-editor.error.binary-file")] = QStringLiteral("Arquivos binários não podem ser editados como texto");
    values[QStringLiteral("code-editor.error.encoding")] = QStringLiteral("O arquivo não contém texto UTF-8 válido");
    values[QStringLiteral("code-editor.error.external-conflict")] = QStringLiteral("O arquivo foi alterado fora do SlotDeck enquanto possui alterações não salvas. O editor preservou o conteúdo não salvo");
    values[QStringLiteral("code-editor.error.external-removed")] = QStringLiteral("O arquivo foi removido fora do SlotDeck enquanto possui alterações não salvas. O editor preservou o conteúdo não salvo");
    values[QStringLiteral("code-editor.close.title")] = QStringLiteral("Descartar Alterações Não Salvas");
    values[QStringLiteral("code-editor.close.file-message")] = QStringLiteral("Fechar este arquivo e descartar suas alterações não salvas?");
    values[QStringLiteral("code-editor.close.workspace-message")] = QStringLiteral("Fechar esta pasta e descartar as alterações não salvas em seus arquivos abertos?");
    values[QStringLiteral("code-editor.close.discard")] = QStringLiteral("Descartar Alterações");
    values[QStringLiteral("code-editor.delete.title")] = QStringLiteral("Excluir Permanentemente");
    values[QStringLiteral("code-editor.delete.file-message")] = QStringLiteral("Excluir permanentemente este arquivo?");
    values[QStringLiteral("code-editor.delete.folder-message")] = QStringLiteral("Excluir permanentemente esta pasta e todo o seu conteúdo?");
    return values;
}

inline TranslationEntries englishLanguageServerErrors() {
    return {{QStringLiteral("code-editor.error.language-server"), QStringLiteral("The language server reported an error")}, {QStringLiteral("code-editor.error.workspace-open"), QStringLiteral("That folder could not be opened as a workspace")}, {QStringLiteral("code-editor.error.charset-unrepresentable"), QStringLiteral("This file holds characters that %1 cannot write, so it was not saved")}, {QStringLiteral("code-editor.settings.files"), QStringLiteral("Files")}, {QStringLiteral("code-editor.settings.default-charset"), QStringLiteral("Encoding when a file is not UTF-8")}, {QStringLiteral("code-editor.settings.default-charset-description"), QStringLiteral("Used when a file carries no byte order mark and does not spell valid UTF-8")}, {QStringLiteral("code-editor.status.encoding-action"), QStringLiteral("Read or write this file in another encoding")}, {QStringLiteral("code-editor.status.reopen-with-encoding"), QStringLiteral("Reopen with encoding")}, {QStringLiteral("code-editor.status.save-with-encoding"), QStringLiteral("Save with encoding")}, {QStringLiteral("code-editor.status.reopen-title"), QStringLiteral("Read this file again?")}, {QStringLiteral("code-editor.status.reopen-message"), QStringLiteral("Reading the file again replaces what you have not saved yet.")}, {QStringLiteral("code-editor.status.reopen-action"), QStringLiteral("Read again")}, {QStringLiteral("code-editor.finder.title"), QStringLiteral("Find a file")}, {QStringLiteral("code-editor.finder.placeholder"), QStringLiteral("Type part of the name")}, {QStringLiteral("code-editor.finder.count"), QStringLiteral("%1 files")}, {QStringLiteral("code-editor.finder.count-capped"), QStringLiteral("The first %1 files of this folder")}, {QStringLiteral("code-editor.tree.filter"), QStringLiteral("Filter files")}, {QStringLiteral("code-editor.tree.filter-placeholder"), QStringLiteral("Part of a name")}, {QStringLiteral("code-editor.problems.filter"), QStringLiteral("Filter by file or message")}};
}

inline TranslationEntries portugueseLanguageServerErrors() {
    return {{QStringLiteral("code-editor.error.language-server"), QStringLiteral("O servidor de linguagem relatou um erro")}};
}

inline TranslationEntries completeCatalog(TranslationEntries values, const TranslationEntries& languageNames) {
    for (auto entry = languageNames.cbegin(); entry != languageNames.cend(); ++entry) {
        values.insert(entry.key(), entry.value());
    }

    return values;
}

inline TranslationCatalog catalog() {
    return {{QStringLiteral("en"), completeCatalog(english(), englishLanguageServerErrors())}, {QStringLiteral("pt"), completeCatalog(portuguese(), portugueseLanguageServerErrors())}};
}

} // namespace slotdeck::plugins::codeeditor::translations
