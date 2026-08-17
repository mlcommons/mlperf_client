import json
import argparse
import os


def load_text_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.read().strip()


def main():
    parser = argparse.ArgumentParser(description="Generate a prompt JSON from a template and system/user text files.")
    parser.add_argument('--template', required=True, help='Model template path')
    parser.add_argument('--system', required=True, help='Text file for the system prompt')
    parser.add_argument('--user', required=True, help='Text file for the user prompt')
    parser.add_argument('--category', required=True, help='Prompt category')
    parser.add_argument('--output', required=True, help='Output file for the final prompt JSON')
    parser.add_argument('--context-length', required=True, type=int, help='Context length')
    parser.add_argument('--max-length', required=True, type=int, help='Generated text max length')
    args = parser.parse_args()

    with open(args.template, 'r', encoding='utf-8') as f:
        template = json.load(f)

    if args.context_length:
        template.setdefault('model_config', {})['context_length'] = args.context_length
    if args.max_length:
        template.setdefault('model_config', {}).setdefault('search', {})['max_length'] = args.max_length

    system_prompt = load_text_file(args.system)
    user_prompt = load_text_file(args.user)

    template['prompts'] = [{"system": system_prompt, "user": user_prompt}]
    template['apply_chat_template'] = True
    template['category'] = args.category

    with open(args.output, 'w', encoding='utf-8') as f:
        json.dump(template, f, indent=2, ensure_ascii=False)

    print(f"Prompt saved to {args.output}")


if __name__ == "__main__":
    main()
