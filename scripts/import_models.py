#!/usr/bin/env python3
"""Rebuild the AI model catalog from a LiteLLM checkout.

Usage: python3 scripts/import_models.py <path to litellm checkout>

Only chat models that call tools are imported, because a task cannot run on a
model that calls none and offering one that always fails is worse than not
offering it at all.
"""

import json
import sys
from pathlib import Path

# our provider identifier for each LiteLLM provider slug.
PROVIDERS = {
    "openai": "openai",
    "anthropic": "anthropic",
    "openrouter": "openrouter",
    "deepseek": "deepseek",
    "xai": "xai",
    "gemini": "gemini",
    "moonshot": "moonshot",
    "groq": "groq",
    "mistral": "mistral",
    "together_ai": "together",
    "fireworks_ai": "fireworks",
    "deepinfra": "deepinfra",
    "perplexity": "perplexity",
    "cerebras": "cerebras",
    "sambanova": "sambanova",
    "nvidia_nim": "nvidia",
    "ollama": "ollama",
}


def wire_id(key: str, slug: str) -> str:
    prefix = f"{slug}/"
    if key.startswith(prefix):
        return key[len(prefix):]
    return key


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2

    source = Path(sys.argv[1]) / "model_prices_and_context_window.json"
    entries = json.loads(source.read_text())

    catalog: dict[str, list[dict]] = {identifier: [] for identifier in PROVIDERS.values()}
    seen: set[tuple[str, str]] = set()
    # every model the source names, so one it names and we exclude is never kept by the merge below.
    declared: set[tuple[str, str]] = set()
    for key, entry in entries.items():
        if not isinstance(entry, dict):
            continue
        slug = entry.get("litellm_provider")
        if slug not in PROVIDERS or entry.get("mode") != "chat":
            continue
        declared.add((PROVIDERS[slug], wire_id(key, slug)))
        if not entry.get("supports_function_calling") or key.startswith("ft:"):
            continue

        # the identifier after the provider slug is what goes on the wire, and it legitimately carries a slash for a hosted open model.
        identifier = wire_id(key, slug)
        provider = PROVIDERS[slug]
        if (provider, identifier) in seen:
            continue
        seen.add((provider, identifier))

        context = entry.get("max_input_tokens") or entry.get("max_tokens") or 0
        output = entry.get("max_output_tokens") or entry.get("max_tokens") or 0
        if not isinstance(context, int) or not isinstance(output, int) or context <= 0 or output <= 0:
            continue

        traits = ["reasoning" if entry.get("supports_reasoning") else "sampling", "function-calling"]
        if entry.get("supports_vision"):
            traits.append("vision")
        # the source omits the flag for the models that accept a system role, which is the majority.
        if entry.get("supports_system_messages", True):
            traits.append("system-prompt")
        model = {"id": identifier, "context": context, "output": output, "traits": traits}
        # the price the service publishes, carried only when the source declares it.
        for field, key in (("input_cost_per_token", "inputCost"), ("output_cost_per_token", "outputCost")):
            price = entry.get(field)
            if isinstance(price, (int, float)) and price >= 0:
                model[key] = float(price)
        catalog[provider].append(model)

    # a model added by hand survives a regeneration, so the file stays the one place models are edited.
    target = Path(__file__).resolve().parent.parent / "plugins" / "ai" / "assets" / "models.json"
    if target.exists():
        existing = json.loads(target.read_text())
        for provider, models in existing.get("providers", {}).items():
            for model in models:
                if (provider, model["id"]) not in seen and (provider, model["id"]) not in declared:
                    seen.add((provider, model["id"]))
                    catalog.setdefault(provider, []).append(model)

    for models in catalog.values():
        models.sort(key=lambda model: model["id"])

    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps({"providers": catalog}, indent=2, sort_keys=True) + "\n")
    total = sum(len(models) for models in catalog.values())
    print(f"wrote {total} models for {len(catalog)} providers to {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
