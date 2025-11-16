import json
import argparse
import os

def load_text_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.read().strip() 

def main():
    parser = argparse.ArgumentParser(description="Generate final prompt from template and text files.")
    parser.add_argument('--template', required=True, help='Model template path')
    parser.add_argument('--model', required=True, help='Model used')
    parser.add_argument('--system', required=True, help='Text file for the system prompt')
    parser.add_argument('--user', required=True, help='Text file for the user prompt')
    parser.add_argument('--category', required=True, help='Prompt category')
    parser.add_argument('--output', required=True, help='Output file for the final prompt JSON')
    parser.add_argument('--context-length', required=True, type=int, help='Context length')
    parser.add_argument('--max-length', required=True, type=int, help='Generated text max length')
    parser.add_argument('--max-total-length', required=True, type=int, help='Max total length')
    args = parser.parse_args()

    with open(os.path.join(args.template), 'r', encoding='utf-8') as f:
        template = json.load(f)

    if args.context_length:
        template['model_config']['model']['context_length'] = args.context_length
    if args.max_length:
        template['model_config']['search']['max_length'] = args.max_length
    if args.max_total_length:
        template['model_config']['search']['max_total_length'] = args.max_total_length
    
    system_prompt = load_text_file(args.system)
    user_prompt = load_text_file(args.user)

    prompt_scheme = {
        "llama2": f"<s>[INST]<<SYS>>{system_prompt}<</SYS>>\n\n{user_prompt}[/INST]",
        "llama3": f"<|begin_of_text|><|start_header_id|>system<|end_header_id|>\n\n{system_prompt}<|eot_id|><|start_header_id|>user<|end_header_id|>\n\n{user_prompt}<|eot_id|>",
        "phi3.5": f"<|system|>{system_prompt}<|end|><|user|>\n\n{user_prompt}<|end|><|assistant|>",
        "phi4": f"<|im_start|>system<|im_sep|>{system_prompt}<|im_end|><|im_start|>user<|im_sep|>\n\n{user_prompt}<|im_end|><|im_start|>assistant<|im_sep|>"
    }

    template['prompts'] = [prompt_scheme[args.model]]
    template['category'] = args.category
    
    with open(args.output, 'w', encoding='utf-8') as f:
        json.dump(template, f, indent=2, ensure_ascii=False)
    
    print(f"Converted prompt saved to {args.output}")

if __name__ == "__main__":
    main()