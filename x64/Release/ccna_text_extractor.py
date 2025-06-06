import requests
from bs4 import BeautifulSoup
import time

def extract_questions_and_answers(url: str):
    print(f"🌐 Extracting from: {url}")
    headers = {'User-Agent': 'Mozilla/5.0'}
    try:
        response = requests.get(url, headers=headers, timeout=30)
        response.raise_for_status()
    except Exception as e:
        print(f"❌ Failed to fetch: {e}")
        return []

    soup = BeautifulSoup(response.content, 'html.parser')
    question_elements = soup.find_all('strong')
    extracted = []
    
    for q_elem in question_elements:
        question_text = q_elem.get_text().strip()
        if not question_text or not (question_text[0].isdigit() or question_text.lower().startswith('q')):
            continue

        # Locate the answer list
        parent = q_elem.parent
        ul = None
        for _ in range(5):
            if parent:
                ul = parent.find_next('ul')
                if ul:
                    break
                parent = parent.next_sibling
        if not ul:
            continue

        correct_answers = []
        for li in ul.find_all('li', recursive=False):
            answer = li.get_text().strip()
            if not answer:
                continue
            if (li.find('span', style=lambda x: x and '#ff0000' in x.lower()) or
                li.find('span', style=lambda x: x and 'color' in x.lower() and 'ff0000' in x.lower()) or
                li.find(attrs={'style': lambda x: x and '#ff0000' in x.lower()})):
                correct_answers.append(answer)
        
        if correct_answers:
            extracted.append({
                'question': question_text,
                'correct_answers': correct_answers,
                'source_url': url
            })
    
    print(f"✅ Found {len(extracted)} questions with correct answers.")
    return extracted

def save_to_text_file(all_questions, filename="ccna_questions.txt"):
    """Save questions to text file format that C++ overlay can read"""
    with open(filename, 'w', encoding='utf-8') as f:
        f.write("# CCNA Questions Database\n")
        f.write(f"# User: TarekBtw | Time: 2025-06-06 13:37:08 UTC\n")
        f.write("# Format: Q#<question>, A:<answer>, URL:<source>\n\n")
        
        question_id = 1
        for q in all_questions:
            f.write(f"Q#{q['question']}\n")
            for answer in q['correct_answers']:
                f.write(f"A:{answer}\n")
            f.write(f"URL:{q['source_url']}\n")
            f.write("\n")  # Empty line between questions
            question_id += 1
    
    print(f"💾 Saved {len(all_questions)} questions to {filename}")
    return len(all_questions)

def main():
    print("🚀 CCNA Question Text Extractor")
    print("👤 User: TarekBtw")
    print("📅 Time: 2025-06-06 13:37:08 UTC")
    print("=" * 60)
    
    urls = [
        "https://itexamanswers.net/ccna-2-v7-modules-1-4-switching-concepts-vlans-and-intervlan-routing-exam-answers.html",
        "https://itexamanswers.net/ccna-2-v7-modules-14-16-routing-concepts-and-configuration-exam-answers.html",
        "https://itexamanswers.net/ccna-3-v7-modules-1-2-ospf-concepts-and-configuration-exam-answers.html"
    ]
    
    try:
        all_questions = []
        
        for url in urls:
            questions = extract_questions_and_answers(url)
            if questions:
                all_questions.extend(questions)
        
        if all_questions:
            count = save_to_text_file(all_questions)
            print(f"\n🎉 Extraction complete! Total questions: {count}")
            print("📄 Text file 'ccna_questions.txt' created for overlay!")
            return 0
        else:
            print("❌ No questions found!")
            return 1
        
    except Exception as e:
        print(f"💥 ERROR: {e}")
        return 1

if __name__ == "__main__":
    exit(main())