# coding=utf-8
# Copyright 2025 The Google Research Authors.
# 2026 MLCommons.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Utility library of instructions."""

import functools
import random
import re
from typing import List

import immutabledict

WORD_LIST = ["western", "sentence", "signal", "dump", "spot", "opposite", "bottom", "potato", "administration", "working", "welcome", "morning", "good", "agency", "primary", "wish", "responsibility", "press", "problem", "president", "steal", "brush", "read", "type", "beat", "trainer", "growth", "lock", "bone", "case", "equal", "comfortable", "region", "replacement", "performance", "mate", "walk", "medicine", "film", "thing", "rock", "tap", "total", "competition", "ease", "south", "establishment", "gather", "parking", "world", "plenty", "breath", "claim", "alcohol", "trade", "dear", "highlight", "street", "matter", "decision", "mess", "agreement", "studio", "coach", "assist", "brain", "wing", "style", "private", "top", "brown", "leg", "buy", "procedure", "method", "speed", "high", "company", "valuable", "pie", "analyst", "session", "pattern", "district", "pleasure", "dinner", "swimming", "joke", "order", "plate", "department", "motor", "cell", "spend", "cabinet", "difference", "power", "examination", "engine", "horse", "dimension", "pay", "toe", "curve", "literature", "bother", "fire", "possibility", "debate", "activity", "passage", "hello", "cycle", "background", "quiet", "author", "effect", "actor", "page", "bicycle", "error", "throat", "attack", "character", "phone", "tea", "increase", "outcome", "file", "specific", "inspector", "internal", "potential", "staff", "building", "employer", "shoe", "hand", "direction", "garden", "purchase", "interview", "study", "recognition", "member", "spiritual", "oven", "sandwich", "weird", "passenger", "particular", "response", "reaction", "size", "variation", "a", "cancel", "candy", "exit", "guest", "condition", "fly", "price", "weakness", "convert", "hotel", "great", "mouth", "mind", "song", "sugar", "suspect", "telephone", "ear", "roof", "paint", "refrigerator", "organization", "jury", "reward", "engineering", "day", "possession", "crew", "bar", "road", "description", "celebration", "score", "mark", "letter", "shower", "suggestion", "sir", "luck", "national", "progress", "hall", "stroke", "theory", "offer", "story", "tax", "definition", "history", "ride", "medium", "opening", "glass", "elevator", "stomach", "question", "ability", "leading", "village", "computer", "city", "grand", "confidence", "candle", "priest", "recommendation", "point", "necessary", "body", "desk", "secret", "horror", "noise", "culture", "warning", "water", "round", "diet", "flower", "bus", "tough", "permission", "week", "prompt", "connection", "abuse", "height", "save", "corner", "border", "stress", "drive", "stop", "rip", "meal", "listen", "confusion", "girlfriend", "living", "relation", "significance", "plan", "creative", "atmosphere", "blame", "invite", "housing", "paper", "drink", "roll", "silver", "drunk", "age", "damage", "smoke", "environment", "pack", "savings", "influence", "tourist", "rain", "post", "sign", "grandmother", "run", "profit", "push", "clerk", "final", "wine", "swim", "pause", "stuff", "singer", "funeral", "average", "source", "scene", "tradition", "personal", "snow", "nobody", "distance", "sort", "sensitive", "animal", "major", "negotiation", "click", "mood", "period", "arrival", "expression", "holiday", "repeat", "dust", "closet", "gold", "bad", "sail", "combination", "clothes", "emphasis", "duty", "black", "step", "school", "jump", "document", "professional", "lip", "chemical", "front", "wake", "while", "inside", "watch", "row", "subject", "penalty", "balance", "possible", "adult", "aside", "sample", "appeal", "wedding", "depth", "king", "award", "wife", "blow", "site", "camp", "music", "safe", "gift", "fault", "guess", "act", "shame", "drama", "capital", "exam", "stupid", "record", "sound", "swing", "novel", "minimum", "ratio", "machine", "shape", "lead", "operation", "salary", "cloud", "affair", "hit", "chapter", "stage", "quantity", "access", "army", "chain", "traffic", "kick", "analysis", "airport", "time", "vacation", "philosophy", "ball", "chest", "thanks", "place", "mountain", "advertising", "red", "past", "rent", "return", "tour", "house", "construction", "net", "native", "war", "figure", "fee", "spray", "user", "dirt", "shot", "task", "stick", "friend", "software", "promotion", "interaction", "surround", "block", "purpose", "practice", "conflict", "routine", "requirement", "bonus", "hole", "state", "junior", "sweet", "catch", "tear", "fold", "wall", "editor", "life", "position", "pound", "respect", "bathroom", "coat", "script", "job", "teach", "birth", "view", "resolve", "theme", "employee", "doubt", "market", "education", "serve", "recover", "tone", "harm", "miss", "union", "understanding", "cow", "river", "association", "concept", "training", "recipe", "relationship", "reserve", "depression", "proof", "hair", "revenue", "independent", "lift", "assignment", "temporary", "amount", "loss", "edge", "track", "check", "rope", "estimate", "pollution", "stable", "message", "delivery", "perspective", "mirror", "assistant", "representative", "witness", "nature", "judge", "fruit", "tip", "devil", "town", "emergency", "upper", "drop", "stay", "human", "neck", "speaker", "network", "sing", "resist", "league", "trip", "signature", "lawyer", "importance", "gas", "choice", "engineer", "success", "part", "external", "worker", "simple", "quarter", "student", "heart", "pass", "spite", "shift", "rough", "lady", "grass", "community", "garage", "youth", "standard", "skirt", "promise", "blind", "television", "disease", "commission", "positive", "energy", "calm", "presence", "tune", "basis", "preference", "head", "common", "cut", "somewhere", "presentation", "current", "thought", "revolution", "effort", "master", "implement", "republic", "floor", "principle", "stranger", "shoulder", "grade", "button", "tennis", "police", "collection", "account", "register", "glove", "divide", "professor", "chair", "priority", "combine", "peace", "extension", "maybe", "evening", "frame", "sister", "wave", "code", "application", "mouse", "match", "counter", "bottle", "half", "cheek", "resolution", "back", "knowledge", "make", "discussion", "screw", "length", "accident", "battle", "dress", "knee", "log", "package", "it", "turn", "hearing", "newspaper", "layer", "wealth", "profile", "imagination", "answer", "weekend", "teacher", "appearance", "meet", "bike", "rise", "belt", "crash", "bowl", "equivalent", "support", "image", "poem", "risk", "excitement", "remote", "secretary", "public", "produce", "plane", "display", "money", "sand", "situation", "punch", "customer", "title", "shake", "mortgage", "option", "number", "pop", "window", "extent", "nothing", "experience", "opinion", "departure", "dance", "indication", "boy", "material", "band", "leader", "sun", "beautiful", "muscle", "farmer", "variety", "fat", "handle", "director", "opportunity", "calendar", "outside", "pace", "bath", "fish", "consequence", "put", "owner", "go", "doctor", "information", "share", "hurt", "protection", "career", "finance", "force", "golf", "garbage", "aspect", "kid", "food", "boot", "milk", "respond", "objective", "reality", "raw", "ring", "mall", "one", "impact", "area", "news", "international", "series", "impress", "mother", "shelter", "strike", "loan", "month", "seat", "anything", "entertainment", "familiar", "clue", "year", "glad", "supermarket", "natural", "god", "cost", "conversation", "tie", "ruin", "comfort", "earth", "storm", "percentage", "assistance", "budget", "strength", "beginning", "sleep", "other", "young", "unit", "fill", "store", "desire", "hide", "value", "cup", "maintenance", "nurse", "function", "tower", "role", "class", "camera", "database", "panic", "nation", "basket", "ice", "art", "spirit", "chart", "exchange", "feedback", "statement", "reputation", "search", "hunt", "exercise", "nasty", "notice", "male", "yard", "annual", "collar", "date", "platform", "plant", "fortune", "passion", "friendship", "spread", "cancer", "ticket", "attitude", "island", "active", "object", "service", "buyer", "bite", "card", "face", "steak", "proposal", "patient", "heat", "rule", "resident", "broad", "politics", "west", "knife", "expert", "girl", "design", "salt", "baseball", "grab", "inspection", "cousin", "couple", "magazine", "cook", "dependent", "security", "chicken", "version", "currency", "ladder", "scheme", "kitchen", "employment", "local", "attention", "manager", "fact", "cover", "sad", "guard", "relative", "county", "rate", "lunch", "program", "initiative", "gear", "bridge", "breast", "talk", "dish", "guarantee", "beer", "vehicle", "reception", "woman", "substance", "copy", "lecture", "advantage", "park", "cold", "death", "mix", "hold", "scale", "tomorrow", "blood", "request", "green", "cookie", "church", "strip", "forever", "beyond", "debt", "tackle", "wash", "following", "feel", "maximum", "sector", "sea", "property", "economics", "menu", "bench", "try", "language", "start", "call", "solid", "address", "income", "foot", "senior", "honey", "few", "mixture", "cash", "grocery", "link", "map", "form", "factor", "pot", "model", "writer", "farm", "winter", "skill", "anywhere", "birthday", "policy", "release", "husband", "lab", "hurry", "mail", "equipment", "sink", "pair", "driver", "consideration", "leather", "skin", "blue", "boat", "sale", "brick", "two", "feed", "square", "dot", "rush", "dream", "location", "afternoon", "manufacturer", "control", "occasion", "trouble", "introduction", "advice", "bet", "eat", "kill", "category", "manner", "office", "estate", "pride", "awareness", "slip", "crack", "client", "nail", "shoot", "membership", "soft", "anybody", "web", "official", "individual", "pizza", "interest", "bag", "spell", "profession", "queen", "deal", "resource", "ship", "guy", "chocolate", "joint", "formal", "upstairs", "car", "resort", "abroad", "dealer", "associate", "finger", "surgery", "comment", "team", "detail", "crazy", "path", "tale", "initial", "arm", "radio", "demand", "single", "draw", "yellow", "contest", "piece", "quote", "pull", "commercial", "shirt", "contribution", "cream", "channel", "suit", "discipline", "instruction", "concert", "speech", "low", "effective", "hang", "scratch", "industry", "breakfast", "lay", "join", "metal", "bedroom", "minute", "product", "rest", "temperature", "many", "give", "argument", "print", "purple", "laugh", "health", "credit", "investment", "sell", "setting", "lesson", "egg", "middle", "marriage", "level", "evidence", "phrase", "love", "self", "benefit", "guidance", "affect", "you", "dad", "anxiety", "special", "boyfriend", "test", "blank", "payment", "soup", "obligation", "reply", "smile", "deep", "complaint", "addition", "review", "box", "towel", "minor", "fun", "soil", "issue", "cigarette", "internet", "gain", "tell", "entry", "spare", "incident", "family", "refuse", "branch", "can", "pen", "grandfather", "constant", "tank", "uncle", "climate", "ground", "volume", "communication", "kind", "poet", "child", "screen", "mine", "quit", "gene", "lack", "charity", "memory", "tooth", "fear", "mention", "marketing", "reveal", "reason", "court", "season", "freedom", "land", "sport", "audience", "classroom", "law", "hook", "win", "carry", "eye", "smell", "distribution", "research", "country", "dare", "hope", "whereas", "stretch", "library", "if", "delay", "college", "plastic", "book", "present", "use", "worry", "champion", "goal", "economy", "march", "election", "reflection", "midnight", "slide", "inflation", "action", "challenge", "guitar", "coast", "apple", "campaign", "field", "jacket", "sense", "way", "visual", "remove", "weather", "trash", "cable", "regret", "buddy", "beach", "historian", "courage", "sympathy", "truck", "tension", "permit", "nose", "bed", "son", "person", "base", "meat", "usual", "air", "meeting", "worth", "game", "independence", "physical", "brief", "play", "raise", "board", "she", "key", "writing", "pick", "command", "party", "yesterday", "spring", "candidate", "physics", "university", "concern", "development", "change", "string", "target", "instance", "room", "bitter", "bird", "football", "normal", "split", "impression", "wood", "long", "meaning", "stock", "cap", "leadership", "media", "ambition", "fishing", "essay", "salad", "repair", "today", "designer", "night", "bank", "drawing", "inevitable", "phase", "vast", "chip", "anger", "switch", "cry", "twist", "personality", "attempt", "storage", "being", "preparation", "bat", "selection", "white", "technology", "contract", "side", "section", "station", "till", "structure", "tongue", "taste", "truth", "difficulty", "group", "limit", "main", "move", "feeling", "light", "example", "mission", "might", "wait", "wheel", "shop", "host", "classic", "alternative", "cause", "agent", "consist", "table", "airline", "text", "pool", "craft", "range", "fuel", "tool", "partner", "load", "entrance", "deposit", "hate", "article", "video", "summer", "feature", "extreme", "mobile", "hospital", "flight", "fall", "pension", "piano", "fail", "result", "rub", "gap", "system", "report", "suck", "ordinary", "wind", "nerve", "ask", "shine", "note", "line", "mom", "perception", "brother", "reference", "bend", "charge", "treat", "trick", "term", "homework", "bake", "bid", "status", "project", "strategy", "orange", "let", "enthusiasm", "parent", "concentrate", "device", "travel", "poetry", "business", "society", "kiss", "end", "vegetable", "employ", "schedule", "hour", "brave", "focus", "process", "movie", "illegal", "general", "coffee", "ad", "highway", "chemistry", "psychology", "hire", "bell", "conference", "relief", "show", "neat", "funny", "weight", "quality", "club", "daughter", "zone", "touch", "tonight", "shock", "burn", "excuse", "name", "survey", "landscape", "advance", "satisfaction", "bread", "disaster", "item", "hat", "prior", "shopping", "visit", "east", "photo", "home", "idea", "father", "comparison", "cat", "pipe", "winner", "count", "lake", "fight", "prize", "foundation", "dog", "keep", "ideal", "fan", "struggle", "peak", "safety", "solution", "hell", "conclusion", "population", "strain", "alarm", "measurement", "second", "train", "race", "due", "insurance", "boss", "tree", "monitor", "sick", "course", "drag", "appointment", "slice", "still", "care", "patience", "rich", "escape", "emotion", "royal", "female", "childhood", "government", "picture", "will", "sock", "big", "gate", "oil", "cross", "pin", "improvement", "championship", "silly", "help", "sky", "pitch", "man", "diamond", "most", "transition", "work", "science", "committee", "moment", "fix", "teaching", "dig", "specialist", "complex", "guide", "people", "dead", "voice", "original", "break", "topic", "data", "degree", "reading", "recording", "bunch", "reach", "judgment", "lie", "regular", "set", "painting", "mode", "list", "player", "bear", "north", "wonder", "carpet", "heavy", "officer", "negative", "clock", "unique", "baby", "pain", "assumption", "disk", "iron", "bill", "drawer", "look", "double", "mistake", "finish", "future", "brilliant", "contact", "math", "rice", "leave", "restaurant", "discount", "sex", "virus", "bit", "trust", "event", "wear", "juice", "failure", "bug", "context", "mud", "whole", "wrap", "intention", "draft", "pressure", "cake", "dark", "explanation", "space", "angle", "word", "efficiency", "management", "habit", "star", "chance", "finding", "transportation", "stand", "criticism", "flow", "door", "injury", "insect", "surprise", "apartment"]  # pylint: disable=line-too-long

# ISO 639-1 codes to language names.
LANGUAGE_CODES = immutabledict.immutabledict({
    "en": "English",
    "es": "Spanish",
    "pt": "Portuguese",
    "ar": "Arabic",
    "hi": "Hindi",
    "fr": "French",
    "ru": "Russian",
    "de": "German",
    "ja": "Japanese",
    "it": "Italian",
    "bn": "Bengali",
    "uk": "Ukrainian",
    "th": "Thai",
    "ur": "Urdu",
    "ta": "Tamil",
    "te": "Telugu",
    "bg": "Bulgarian",
    "ko": "Korean",
    "pl": "Polish",
    "he": "Hebrew",
    "fa": "Persian",
    "vi": "Vietnamese",
    "ne": "Nepali",
    "sw": "Swahili",
    "kn": "Kannada",
    "mr": "Marathi",
    "gu": "Gujarati",
    "pa": "Punjabi",
    "ml": "Malayalam",
    "fi": "Finnish",
    })

PLURALS = {
    'abscissa': 'abscissae',
    'addendum': 'addenda',
    'agendum': 'agenda',
    'alga': 'algae',
    'alumna': 'alumnae',
    'alumnus': 'alumni',
    'alveolus': 'alveoli',
    'analysis': 'analyses',
    'antithesis': 'antitheses',
    'aphelion': 'aphelia',
    'axis': 'axes',
    'bacillus': 'bacilli',
    'bacterium': 'bacteria',
    'baculum': 'bacula',
    'basis': 'bases',
    'businessman': 'businessmen',
    'calf': 'calves',
    'candelabrum': 'candelabra',
    'chairman': 'chairmen',
    'child': 'children',
    'cloaca': 'cloacae',
    'codex': 'codices',
    'consortium': 'consortia',
    'corpus': 'corpora',
    'cortex': 'cortices',
    'cranium': 'crania',
    'crisis': 'crises',
    'criterion': 'criteria',
    'curriculum': 'curricula',
    'cystoma': 'cystomata',
    'datum': 'data',
    'desideratum': 'desiderata',
    'diagnosis': 'diagnoses',
    'dictum': 'dicta',
    'die': 'dice',
    'djinni': 'djinn',
    'dogma': 'dogmata',
    'elf': 'elves',
    'ellipsis': 'ellipses',
    'emphasis': 'emphases',
    'emporium': 'emporia',
    'encomium': 'encomia',
    'ephemeris': 'ephemerides',
    'erratum': 'errata',
    'extremum': 'extrema',
    'fez': 'fezzes',
    'fibula': 'fibulae',
    'foot': 'feet',
    'foramen': 'foramina',
    'fungus': 'fungi',
    'ganglion': 'ganglia',
    'gentleman': 'gentlemen',
    'genus': 'genera',
    'glomerulus': 'glomeruli',
    'goose': 'geese',
    'goy': 'goyim',
    'graffito': 'graffiti',
    'gumma': 'gummata',
    'half': 'halves',
    'hamulus': 'hamuli',
    'honorarium': 'honoraria',
    'hoof': 'hooves',
    'humerus': 'humeri',
    'hyperbaton': 'hyperbata',
    'hyperbola': 'hyperbolae',
    'hypothesis': 'hypotheses',
    'ilium': 'ilia',
    'incubus': 'incubi',
    'interregnum': 'interregna',
    'interstitium': 'interstitia',
    'knife': 'knives',
    'larva': 'larvae',
    'leaf': 'leaves',
    'life': 'lives',
    'loaf': 'loaves',
    'loculus': 'loculi',
    'locus': 'loci',
    'looey': 'looies',
    'louse': 'lice',
    'lumen': 'lumina',
    'lustrum': 'lustra',
    'lymphoma': 'lymphomata',
    'man': 'men',
    'matrix': 'matrices',
    'maximum': 'maxima',
    'medium': 'media',
    'memorandum': 'memoranda',
    'meniscus': 'menisci',
    'millennium': 'millennia',
    'minimum': 'minima',
    'minutia': 'minutiae',
    'momentum': 'momenta',
    'mouse': 'mice',
    'murex': 'murices',
    'mythos': 'mythoi',
    'nemesis': 'nemeses',
    'neurosis': 'neuroses',
    'noumenon': 'noumena',
    'nucleolus': 'nucleoli',
    'nucleus': 'nuclei',
    'oasis': 'oases',
    'occiput': 'occipita',
    'omphalos': 'omphaloi',
    'optimum': 'optima',
    'ovum': 'ova',
    'ox': 'oxen',
    'paralysis': 'paralyses',
    'parenthesis': 'parentheses',
    'passerby': 'passersby',
    'perihelion': 'perihelia',
    'person': 'people',
    'phalanx': 'phalanges',
    'phenomenon': 'phenomena',
    'phylum': 'phyla',
    'policeman': 'policemen',
    'polyhedron': 'polyhedra',
    'pontifex': 'pontifices',
    'prognosis': 'prognoses',
    'prolegomenon': 'prolegomena',
    'quantum': 'quanta',
    'quiz': 'quizzes',
    'radius': 'radii',
    'sarcophagus': 'sarcophagi',
    'scarf': 'scarves',
    'scrotum': 'scrota',
    'self': 'selves',
    'shelf': 'shelves',
    'silex': 'silices',
    'simulacrum': 'simulacra',
    'spokesman': 'spokesmen',
    'spectrum': 'spectra',
    'speculum': 'specula',
    'stimulus': 'stimuli',
    'stratum': 'strata',
    'succubus': 'succubi',
    'syconium': 'syconia',
    'synopsis': 'synopses',
    'synthesis': 'syntheses',
    'testis': 'testes',
    'that': 'those',
    'thesis': 'theses',
    'thief': 'thieves',
    'this': 'these',
    'thrombus': 'thrombi',
    'tooth': 'teeth',
    'torus': 'tori',
    'trapezium': 'trapezia',
    'umbilicus': 'umbilici',
    'velum': 'vela',
    'vertebra': 'vertebrae',
    'vertex': 'vertices',
    'viscus': 'viscera',
    'vita': 'vitae',
    'vortex': 'vortices',
    'wharf': 'wharves',
    'wife': 'wives',
    'wolf': 'wolves',
    'woman': 'women',
}

SINGULARS = {
    'abscissae': 'abscissa',
    'addenda': 'addendum',
    'agenda': 'agendum',
    'algae': 'alga',
    'alumnae': 'alumna',
    'alumni': 'alumnus',
    'alveoli': 'alveolus',
    'analyses': 'analysis',
    'antitheses': 'antithesis',
    'aphelia': 'aphelion',
    'axes': 'axis',
    'bacilli': 'bacillus',
    'bacteria': 'bacterium',
    'bacula': 'baculum',
    'bases': 'basis',
    'businessmen': 'businessman',
    'calves': 'calf',
    'candelabra': 'candelabrum',
    'chairmen': 'chairman',
    'children': 'child',
    'cloacae': 'cloaca',
    'codices': 'codex',
    'consortia': 'consortium',
    'corpora': 'corpus',
    'cortices': 'cortex',
    'crania': 'cranium',
    'crises': 'crisis',
    'criteria': 'criterion',
    'curricula': 'curriculum',
    'cystomata': 'cystoma',
    'data': 'datum',
    'desiderata': 'desideratum',
    'diagnoses': 'diagnosis',
    'dicta': 'dictum',
    'dice': 'die',
    'djinn': 'djinni',
    'dogmata': 'dogma',
    'elves': 'elf',
    'ellipses': 'ellipsis',
    'emphases': 'emphasis',
    'emporia': 'emporium',
    'encomia': 'encomium',
    'ephemerides': 'ephemeris',
    'errata': 'erratum',
    'extrema': 'extremum',
    'fezzes': 'fez',
    'fibulae': 'fibula',
    'feet': 'foot',
    'foramina': 'foramen',
    'fungi': 'fungus',
    'ganglia': 'ganglion',
    'gentlemen': 'gentleman',
    'genera': 'genus',
    'glomeruli': 'glomerulus',
    'geese': 'goose',
    'goyim': 'goy',
    'graffiti': 'graffito',
    'gummata': 'gumma',
    'halves': 'half',
    'hamuli': 'hamulus',
    'honoraria': 'honorarium',
    'hooves': 'hoof',
    'humeri': 'humerus',
    'hyperbata': 'hyperbaton',
    'hyperbolae': 'hyperbola',
    'hypotheses': 'hypothesis',
    'ilia': 'ilium',
    'incubi': 'incubus',
    'interregna': 'interregnum',
    'interstitia': 'interstitium',
    'knives': 'knife',
    'larvae': 'larva',
    'leaves': 'leaf',
    'lives': 'life',
    'loaves': 'loaf',
    'loculi': 'loculus',
    'loci': 'locus',
    'looies': 'looey',
    'lice': 'louse',
    'lumina': 'lumen',
    'lustra': 'lustrum',
    'lymphomata': 'lymphoma',
    'men': 'man',
    'matrices': 'matrix',
    'maxima': 'maximum',
    'media': 'medium',
    'memoranda': 'memorandum',
    'menisci': 'meniscus',
    'millennia': 'millennium',
    'minima': 'minimum',
    'minutiae': 'minutia',
    'momenta': 'momentum',
    'mice': 'mouse',
    'murices': 'murex',
    'mythoi': 'mythos',
    'nemeses': 'nemesis',
    'neuroses': 'neurosis',
    'noumena': 'noumenon',
    'nucleoli': 'nucleolus',
    'nuclei': 'nucleus',
    'oases': 'oasis',
    'occipita': 'occiput',
    'omphaloi': 'omphalos',
    'optima': 'optimum',
    'ova': 'ovum',
    'oxen': 'ox',
    'paralyses': 'paralysis',
    'parentheses': 'parenthesis',
    'passersby': 'passerby',
    'perihelia': 'perihelion',
    'people': 'person',
    'phalanges': 'phalanx',
    'phenomena': 'phenomenon',
    'phyla': 'phylum',
    'policemen': 'policeman',
    'polyhedra': 'polyhedron',
    'pontifices': 'pontifex',
    'prognoses': 'prognosis',
    'prolegomena': 'prolegomenon',
    'quanta': 'quantum',
    'quizzes': 'quiz',
    'radii': 'radius',
    'sarcophagi': 'sarcophagus',
    'scarves': 'scarf',
    'scrota': 'scrotum',
    'selves': 'self',
    'shelves': 'shelf',
    'silices': 'silex',
    'simulacra': 'simulacrum',
    'spokesmen': 'spokesman',
    'spectra': 'spectrum',
    'specula': 'speculum',
    'stimuli': 'stimulus',
    'strata': 'stratum',
    'succubi': 'succubus',
    'syconia': 'syconium',
    'synopses': 'synopsis',
    'syntheses': 'synthesis',
    'testes': 'testis',
    'those': 'that',
    'theses': 'thesis',
    'thieves': 'thief',
    'these': 'this',
    'thrombi': 'thrombus',
    'teeth': 'tooth',
    'tori': 'torus',
    'trapezia': 'trapezium',
    'umbilici': 'umbilicus',
    'vela': 'velum',
    'vertebrae': 'vertebra',
    'vertices': 'vertex',
    'viscera': 'viscus',
    'vitae': 'vita',
    'vortices': 'vortex',
    'wharves': 'wharf',
    'wives': 'wife',
    'wolves': 'wolf',
    'women': 'woman',
}

_ALPHABETS = "([A-Za-z])"
_PREFIXES = "(Mr|St|Mrs|Ms|Dr)[.]"
_SUFFIXES = "(Inc|Ltd|Jr|Sr|Co)"
_STARTERS = r"(Mr|Mrs|Ms|Dr|Prof|Capt|Cpt|Lt|He\s|She\s|It\s|They\s|Their\s|Our\s|We\s|But\s|However\s|That\s|This\s|Wherever)"
_ACRONYMS = "([A-Z][.][A-Z][.](?:[A-Z][.])?)"
_WEBSITES = "[.](com|net|org|io|gov|edu|me)"
_DIGITS = "([0-9])"
_MULTIPLE_DOTS = r"\.{2,}"


def split_into_sentences(text):
  """Split the text into sentences.

  Args:
    text: A string that consists of more than or equal to one sentences.

  Returns:
    A list of strings where each string is a sentence.
  """
  text = " " + text + "  "
  text = text.replace("\n", " ")
  text = re.sub(_PREFIXES, "\\1<prd>", text)
  text = re.sub(_WEBSITES, "<prd>\\1", text)
  text = re.sub(_DIGITS + "[.]" + _DIGITS, "\\1<prd>\\2", text)
  text = re.sub(
      _MULTIPLE_DOTS,
      lambda match: "<prd>" * len(match.group(0)) + "<stop>",
      text,
  )
  if "Ph.D" in text:
    text = text.replace("Ph.D.", "Ph<prd>D<prd>")
  text = re.sub(r"\s" + _ALPHABETS + "[.] ", " \\1<prd> ", text)
  text = re.sub(_ACRONYMS + " " + _STARTERS, "\\1<stop> \\2", text)
  text = re.sub(
      _ALPHABETS + "[.]" + _ALPHABETS + "[.]" + _ALPHABETS + "[.]",
      "\\1<prd>\\2<prd>\\3<prd>",
      text,
  )
  text = re.sub(
      _ALPHABETS + "[.]" + _ALPHABETS + "[.]", "\\1<prd>\\2<prd>", text
  )
  text = re.sub(" " + _SUFFIXES + "[.] " + _STARTERS, " \\1<stop> \\2", text)
  text = re.sub(" " + _SUFFIXES + "[.]", " \\1<prd>", text)
  text = re.sub(" " + _ALPHABETS + "[.]", " \\1<prd>", text)
  if "”" in text:
    text = text.replace(".”", "”.")
  if '"' in text:
    text = text.replace('."', '".')
  if "!" in text:
    text = text.replace('!"', '"!')
  if "?" in text:
    text = text.replace('?"', '"?')
  text = text.replace(".", ".<stop>")
  text = text.replace("?", "?<stop>")
  text = text.replace("!", "!<stop>")
  text = text.replace("<prd>", ".")
  sentences = text.split("<stop>")
  sentences = [s.strip() for s in sentences]
  if sentences and not sentences[-1]:
    sentences = sentences[:-1]
  return sentences


def count_words(text):
  """Counts the number of words."""
  count = 0
  in_word = False

  for c in text:
    if c.isalnum():
      if not in_word:
        in_word = True
        count += 1
    else:
      in_word = False
  return count


def word_before_dot(s, i):
    start = i
    while start > 0 and s[start - 1].isalpha():
        start -= 1
    return s[start:i]

def word_after_dot(s, i):
    end = i + 1
    while end < len(s) and s[end].isalpha():
        end += 1
    return s[i + 1:end]

def is_letter(c):
    return c.isalpha()

def is_digit(c):
    return '0' <= c <= '9'

def is_mark(c):
    return c == '.' or c == '!' or c == '?'

# --- abbreviation logic ---

def is_initialism(s, i):
    j = i
    count = 0

    while j > 0 and s[j - 1].isupper():
        if j + 1 < len(s) and s[j] == '.':
            count += 1
            j -= 2
        else:
            break

    # check if followed by another X. for first '.'
    if count == 1:
        if i + 2 < len(s) and s[i + 1].isupper() and s[i + 2] == '.':
            count = 2

    return count >= 2

def is_latin_abbrev(s, i):
    if i < 3:
        return False
    return (
        s[i - 3].islower() and
        s[i - 2] == '.' and
        s[i - 1].islower() and
        s[i] == '.'
    )

def is_title_abbrev(s, i):
    titles = {"Mr", "Mrs", "Ms", "Dr", "Prof", "Sr", "Jr"}
    word = word_before_dot(s, i)
    return bool(word) and word in titles

def is_enumeration_prefix(s, i):
    if i == 0:
        return False

    # Must be followed by space or newline
    if i + 1 >= len(s) or (s[i + 1] != ' ' and s[i + 1] != '\n'):
        return False

    start = i - 1

    # ---- Numeric enumeration: 1. / 10. ----
    if is_digit(s[start]):
        while start > 0 and is_digit(s[start - 1]):
            start -= 1

    # ---- Letter enumeration: a. / A. ----
    elif is_letter(s[start]) and start > 0 and is_letter(s[start - 1]):
        return False

    # General check
    if start == 0:
        return True

    prev = s[start - 1]
    if prev == ' ' or prev == '\n' or is_mark(prev):
        return True

    return False

def is_domain_suffix(s, i):
    tlds = {"com", "net", "org", "io", "gov", "edu", "me"}

    if i + 1 >= len(s):
        return False

    suffix = word_after_dot(s, i)
    return suffix in tlds

def is_decimal_point(s, i):
    if i == 0 or i + 1 >= len(s):
        return False
    return is_digit(s[i - 1]) and is_digit(s[i + 1])

def is_abbreviation(s, i):
    return (
        is_initialism(s, i) or
        is_latin_abbrev(s, i) or
        is_title_abbrev(s, i)
    )

def abbreviation_blocks_sentence(s, i):
    if not is_abbreviation(s, i):
        return False

    # skip spaces
    j = i + 1
    while j < len(s) and s[j] == ' ':
        j += 1

    # If next token is lowercase, it's mid-sentence
    if j < len(s) and s[j].islower():
        return True

    return False

# --- sentence ending ---

def ends_sentence(s, i):
    c = s[i]

    if not is_mark(c):
        return False

    # collapse runs ?!...
    if i + 1 < len(s) and is_mark(s[i + 1]):
        return False

    if c == '.':
        if is_decimal_point(s, i):
            return False
        if is_enumeration_prefix(s, i):
            return False
        if abbreviation_blocks_sentence(s, i):
            return False
        if is_domain_suffix(s, i):
            return False

    return True

def count_sentences(text):
  """Count the number of sentences."""
  count = 0
  for i in range(len(text)):
      if ends_sentence(text, i):
          count += 1
  return count

def to_lower_ascii(s):
  out = []
  for c in s:
    out.append(c.lower())
  return "".join(out)

def is_word_char(c):
  return c.isalnum() or c == '_'

def contains_word(text, word):
  if word == "":
    return False

  t = to_lower_ascii(text)
  w = to_lower_ascii(word)

  pos = 0
  while True:
    pos = t.find(w, pos)
    if pos == -1:
      break

    left_ok = (pos == 0) or (not is_word_char(t[pos - 1]))
    end = pos + len(w)
    right_ok = (end == len(t)) or (not is_word_char(t[end]))

    if left_ok and right_ok:
      return True

    pos += 1  # continue searching (overlapping-safe)

  return False

def find_containing_word(text: str,
                         keyword: str,
                         pos: int):
    if not keyword or pos >= len(text):
        return None

    t = to_lower_ascii(text)
    k = to_lower_ascii(keyword)

    pos = t.find(k, pos)
    if pos == -1:
        return None

    # Expand left to word boundary
    start = pos
    while start > 0 and is_word_char(t[start - 1]):
        start -= 1

    # Expand right to word boundary
    end = pos + len(k)
    while end < len(t) and is_word_char(t[end]):
        end += 1

    # Extract original (not lowercased) word
    containing_word = text[start:end]
    return start, containing_word


def contains_none(text, words):
  for w in words:
    if contains_word(text, w):
      return False
  return True

def contains_string(text, substring):
  return substring.lower() in text.lower()


def ends_with(s, suf, threshold):
  if len(s) < len(suf):
    return False

  a = s[-(len(suf) + threshold):].lower()
  b = suf.lower()

  return a == b if threshold == 0 else b in a


def starts_with(s, prf, threshold):
  if len(s) < len(prf):
    return False

  a = s[:len(prf) + threshold].lower()
  b = prf.lower()

  return a == b if threshold == 0 else b in a


def generate_keywords(num_keywords):
  """Randomly generates a few keywords."""
  return random.sample(WORD_LIST, k=num_keywords)
