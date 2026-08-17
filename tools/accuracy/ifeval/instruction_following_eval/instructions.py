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

"""Library of instructions."""
import collections
import json
import random
import re
import string
from typing import Dict, Optional, Sequence, Union

from absl import logging
import pycld2 as cld2
import Stemmer

from instruction_following_eval import instructions_util


_InstructionArgsDtype = Optional[Dict[str, Union[int, str, Sequence[str]]]]

_LANGUAGES = instructions_util.LANGUAGE_CODES

# The relational operation for comparison.
_COMPARISON_RELATION = ("less than", "at least")

# The maximum number of sentences.
_MAX_NUM_SENTENCES = 20

# The number of placeholders.
_NUM_PLACEHOLDERS = 4

# The number of bullet lists.
_NUM_BULLETS = 5

# The options of constrained response.
_CONSTRAINED_RESPONSE_OPTIONS = (
    "My answer is yes.", "My answer is no.", "My answer is maybe.")

# The options of starter keywords.
_STARTER_OPTIONS = ("I would say", "My answer is", "I believe",
                    "In my opinion", "I think", "I reckon", "I feel",
                    "From my perspective", "As I see it", "According to me",
                    "As far as I'm concerned", "To my understanding",
                    "In my view", "My take on it is", "As per my perception")

# The options of ending keywords.
# TODO(jeffreyzhou) add more ending options
_ENDING_OPTIONS = ("Any other questions?",
                   "Is there anything else I can help with?")

# The number of highlighted sections.
_NUM_HIGHLIGHTED_SECTIONS = 4

# The section spliter.
_SECTION_SPLITER = ("Section", "SECTION")

# The number of sections.
_NUM_SECTIONS = 5

# The number of paragraphs.
_NUM_PARAGRAPHS = 5

# The postscript marker.
_POSTSCRIPT_MARKER = ("P.S.", "P.P.S")

# The number of keywords.
_NUM_KEYWORDS = 2

# The occurrences of a single keyword.
_KEYWORD_FREQUENCY = 3

# The occurrences of a single letter.
_LETTER_FREQUENCY = 10

# The occurrences of words with all capital letters.
_ALL_CAPITAL_WORD_FREQUENCY = 20

# The number of words in the response.
_NUM_WORDS_LOWER_LIMIT = 100
_NUM_WORDS_UPPER_LIMIT = 500


class Instruction:
  """An instruction template."""

  def __init__(self, instruction_id):
    self.id = instruction_id

  def build_description(self, **kwargs):
    raise NotImplementedError("`build_description` not implemented.")

  def get_instruction_args(self):
    raise NotImplementedError("`get_instruction_args` not implemented.")

  def get_instruction_args_keys(self):
    raise NotImplementedError("`get_instruction_args_keys` not implemented.")

  def check_following(self, value):
    raise NotImplementedError("`check_following` not implemented.")


class ResponseLanguageChecker(Instruction):
  """Check the language of the entire response."""

  def build_description(self, *, language = None):
    """Build the instruction description.

    Args:
      language: A string representing the expected language of the response. The
        language has to comply to the 97 types defined in
        `langid.py` (https://pypi.org/project/langid/1.1.5/), which follows
        ISO 639-1 codes (https://en.wikipedia.org/wiki/List_of_ISO_639-1_codes);
        for example, `en` for English, `zh` for Chinese, `fr` for French.

    Returns:
      A string representing the instruction description.
    """
    self._language = language
    if self._language is None:
      self._language = random.choice(list(_LANGUAGES.keys()))
    # TODO(tianjianlu): opens the description generation to more choices.
    self._description_pattern = (
        "Your ENTIRE response should be in {language} language, no other " +
        "language is allowed.")
    return self._description_pattern.format(language=_LANGUAGES[self._language])

  def get_instruction_args(self):
    """Returns the keyward args of `build_description`."""
    return {"language": self._language}

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["language"]

  def check_following(self, value):
    """Check if the language of the entire response follows the instruction.

    Args:
      value: A string representing the response.

    Returns:
      True if the language of `value` follows instruction; otherwise False.
    """
    assert isinstance(value, str)

    try:
      isReliable, textBytesFound, details = cld2.detect(value.encode("utf-8"))
      return details[0][1] == self._language
    except Exception as e:
      # Count as instruction is followed.
      logging.error(
          "Unable to detect language for text %s due to %s", value, e
      )  # refex: disable=pytotw.037
      return True


class NumberOfSentences(Instruction):
  """Check the number of sentences."""

  def build_description(self, *, num_sentences = None,
                        relation = None):
    """Build the instruction description.

    Args:
      num_sentences: An integer specifying the number of sentences as a
        threshold.
      relation: A string in (`less than`, `at least`), defining the relational
        operator for comparison.
        Two relational comparisons are supported for now:
        if 'less than', the actual number of sentences < the threshold;
        if 'at least', the actual number of sentences >= the threshold.

    Returns:
      A string representing the instruction description.
    """
    # The number of sentences as a threshold for comparison.
    self._num_sentences_threshold = num_sentences
    if (self._num_sentences_threshold is None or
        self._num_sentences_threshold < 0):
      self._num_sentences_threshold = random.randint(1, _MAX_NUM_SENTENCES)

    if relation is None:
      self._comparison_relation = random.choice(_COMPARISON_RELATION)
    elif relation not in _COMPARISON_RELATION:
      raise ValueError("The supported relation for comparison must be in "
                       f"{_COMPARISON_RELATION}, but {relation} is given.")
    else:
      self._comparison_relation = relation

    self._description_pattern = (
        "Your response should contain {relation} {num_sentences} sentences.")
    return self._description_pattern.format(
        relation=self._comparison_relation,
        num_sentences=self._num_sentences_threshold)

  def get_instruction_args(self):
    """Returns the keyward args of `build_description`."""
    return {"num_sentences": self._num_sentences_threshold,
            "relation": self._comparison_relation}

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["num_sentences", "relation"]

  def check_following(self, value):
    """Check if the number of sentences follows the instruction.

    Args:
      value: A string representing the response.

    Returns:
      True if the response follows the instruction.

    Raise:
        ValueError if the string in `instruction_args` is not in
        [`less_than`, `at_least`].
    """
    num_sentences = instructions_util.count_sentences(value)
    if self._comparison_relation == _COMPARISON_RELATION[0]:
      return num_sentences < self._num_sentences_threshold
    elif self._comparison_relation == _COMPARISON_RELATION[1]:
      return num_sentences >= self._num_sentences_threshold  # pytype: disable=bad-return-type


class PlaceholderChecker(Instruction):
  """Check the placeholders in template writing."""

  def build_description(self, *, num_placeholders = None):
    """Build the instruction description.

    Args:
      num_placeholders: An integer denoting the minimum number of
        placeholders required in the response.

    Returns:
      A string representing the instruction description.
    """
    self._num_placeholders = num_placeholders
    if self._num_placeholders is None or self._num_placeholders < 0:
      self._num_placeholders = random.randint(1, _NUM_PLACEHOLDERS)
    self._description_pattern = (
        "The response must contain at least {num_placeholders} placeholders " +
        "represented by square brackets, such as [address].")
    return self._description_pattern.format(
        num_placeholders=self._num_placeholders)

  def get_instruction_args(self):
    """Returns the keyward args of `build_description`."""
    return {"num_placeholders": self._num_placeholders}

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["num_placeholders"]

  def check_following(self, value):
    """Check if the number of placeholders follows the instruction.

    Args:
      value: A string representing the response.

    Returns:
      True if the actual number of placeholders in the response is greater than
      or equal to `num_placeholders`; otherwise, False.
    """
    count = 0
    pos = 0

    while pos < len(value) and int(count) < self._num_placeholders:
        open_ = value.find('[', pos)
        if open_ == -1:
            break

        close = value.find(']', open_ + 1)
        if close == -1:
            break

        # non-empty inner
        if close > open_ + 1:
            count += 1

        # continue after this closing bracket
        pos = close + 1
    return count >= self._num_placeholders


class BulletListChecker(Instruction):
  """Checks the bullet list in the prompt."""

  def build_description(self, *, num_bullets = None):
    """Build the instruction description.

    Args:
      num_bullets: An integer specifying the exact number of bullet lists
        that is required to appear in the response.

    Returns:
      A string representing the instruction description.
    """
    self._num_bullets = num_bullets
    if self._num_bullets is None or self._num_bullets < 0:
      self._num_bullets = random.randint(1, _NUM_BULLETS)
    self._description_pattern = (
        "Your answer must contain exactly {num_bullets} bullet points. " +
        "Use the markdown bullet points such as:\n" +
        "* This is point 1. \n" +
        "* This is point 2")
    return self._description_pattern.format(
        num_bullets=self._num_bullets)

  def get_instruction_args(self):
    """Returns the keyward args of `build_description`."""
    return {"num_bullets": self._num_bullets}

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["num_bullets"]

  def SplitLines(self, s):
    out = []
    for line in s.splitlines(keepends=False):
      out.append(line)
    return out

  def check_following(self, value):
    r"""Check if the number of bullet lists meets the requirement.

    Args:
      value: A string representing the response. The response is expected to
        contain some bullet lists that start with `\*`.

    Returns:
      True if the actual number of bullet lists in the response meets the
      requirement.
    """
    count = 0
    for line in self.SplitLines(value):
        t = line.strip()
        if t.rfind("* ", 0) == 0 or t.rfind("- ", 0) == 0:
            count += 1
            continue
    return count == self._num_bullets


class ConstrainedResponseChecker(Instruction):
  """Checks the constrained response."""

  def build_description(self):
    """Build the instruction description."""
    # A sequence of string(s) representing the options of the expected response.
    self._constrained_responses = _CONSTRAINED_RESPONSE_OPTIONS
    self._description_pattern = (
        "Answer with one of the following options: {response_options}")
    return self._description_pattern.format(
        response_options=self._constrained_responses)

  def get_instruction_args(self):
    """Returns the keyward args of `build_description`."""
    return None

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return []

  def check_following(self, value):
    """Checks if the response matches the constrained options.

    Args:
      value: A string representing the response.

    Returns:
      True if the actual response contains one of the options in the constrained
      responses; otherwise False.
    """
    aYes = "My answer is yes."
    aNo = "My answer is no."
    aMaybe = "My answer is maybe."
    sizeThreshold = 3

    return (
            (value.find(aYes) != -1 and
             len(value) <= sizeThreshold + len(aYes)) or
            (value.find(aNo) != -1 and
             len(value) <= sizeThreshold + len(aNo)) or
            (value.find(aMaybe) != -1 and
             len(value) <= sizeThreshold + len(aMaybe))
        )


class HighlightSectionChecker(Instruction):
  """Checks the highlighted section."""

  def build_description(self, *, num_highlights = None):
    """Build the instruction description.

    Args:
      num_highlights: An integer specifying the minimum number of highlighted
        sections.

    Returns:
      A string representing the instruction description.
    """
    self._num_highlights = num_highlights
    if self._num_highlights is None or self._num_highlights < 0:
      self._num_highlights = random.randint(1, _NUM_HIGHLIGHTED_SECTIONS)

    self._description_pattern = (
        "Highlight at least {num_highlights} sections in your answer with " +
        "markdown, i.e. *highlighted section*.")

    return self._description_pattern.format(num_highlights=self._num_highlights)

  def get_instruction_args(self):
    """Returns the keyward args of `build_description`."""
    return {"num_highlights": self._num_highlights}

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["num_highlights"]

  def check_following(self, value):
    """Checks if the number of highlighted sections meets the requirement.

    Args:
      value: a string repesenting the response. The response is expected to
        contain highlighted sections in the format of *highlighted*.

    Returns:
      True if the actual number of highlighted sections in the format of
      *highlighed sections* meets the minimum requirement; otherwise False.
    """
    count = 0
    pos = 0

    while True:
      # find opening '*'
      open_ = value.find('*', pos)
      if open_ == -1:
        break

      # need at least one non-*\r\n char after the opener
      if open_ + 1 >= len(value):
        break
      next_ = value[open_ + 1]
      if next_ == '*' or next_ == '\n' or next_ == '\r':
        pos = open_ + 1  # not a valid start; try from the next '*'
        continue

      # find the first '*' or newline after the opener
      stop_candidates = []
      for ch in ('*', '\r', '\n'):
        idx = value.find(ch, open_ + 1)
        if idx != -1:
          stop_candidates.append(idx)

      if not stop_candidates:
        break

      stop = min(stop_candidates)

      if value[stop] == '*':
        # we have "*...*" with no '*' or newline inside
        count += 1
        pos = stop + 1  # continue after the closing '*'
      else:
        # newline encountered before a closing '*': this opener can't match
        pos = stop + 1  # continue scanning after the newline

    return count >= self._num_highlights


class SectionChecker(Instruction):
  """Checks the sections."""

  def build_description(self, *, section_spliter = None,
                        num_sections = None):
    """Build the instruction description.

    Args:
      section_spliter: A string represents the section spliter keyword that
        marks a new section, i.e., `Section` or `SECTION`.
      num_sections: An integer specifying the number of sections.

    Returns:
      A string representing the instruction description.
    """
    self._section_spliter = section_spliter.strip() if isinstance(
        section_spliter, str) else section_spliter
    if self._section_spliter is None:
      self._section_spliter = random.choice(_SECTION_SPLITER)

    self._num_sections = num_sections
    if self._num_sections is None or self._num_sections < 0:
      self._num_sections = random.randint(1, _NUM_SECTIONS)

    self._description_pattern = (
        "Your response must have {num_sections} sections. Mark the beginning " +
        "of each section with {section_spliter} X, such as:\n" +
        "{section_spliter} 1\n" +
        "[content of section 1]\n" +
        "{section_spliter} 2\n" +
        "[content of section 2]")

    return self._description_pattern.format(
        num_sections=self._num_sections,
        section_spliter=self._section_spliter)

  def get_instruction_args(self):
    """Returns the keyward args of `build_description`."""
    return {"section_spliter": self._section_spliter,
            "num_sections": self._num_sections}

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["section_spliter", "num_sections"]


  def CountNonEmpty(self, v):
    c = 0
    for p in v:
      if p.strip() != "":
        c += 1
    return c


  def isnum(self, text, pos):
    if pos < 0 or pos >= len(text):
      return False
    c = text[pos]
    return ('0' <= c <= '9') or c == 'I' or c == 'V' or c == 'X'


  def SplitByDelim(self, s, delim):
    if delim == "":
      return [s]

    parts = []

    start = s.find(delim)

    while True:
      if start == -1:
        break

      pos = s.find(delim, start + len(delim))
      if pos == -1:
        parts.append(s[start:])
        break

      next_index = pos + len(delim) + 1
      if not self.isnum(s, next_index):
        start = pos
        continue

      parts.append(s[start:pos])
      start = pos

    return parts

  def check_following(self, value):
    """Checks the response contains multiple sections.

    Args:
      value: A string representing the response. The response is expected
        to contain multiple sections (number of sections is greater than 1).
        A new section starts with `Section 1`, where the number denotes the
        section index.

    Returns:
      True if the number of sections in the response is greater than or equal to
      the minimum number of sections; otherwise, False.
    """
    parts = self.SplitByDelim(value, self._section_spliter)
    count = self.CountNonEmpty(parts)

    if value.find("******") != -1:
      count //= 2
    return count >= self._num_sections


class ParagraphChecker(Instruction):
  """Checks the paragraphs."""

  def build_description(self, *, num_paragraphs = None):
    """Build the instruction description.

    Args:
      num_paragraphs: An integer specifying the number of paragraphs.

    Returns:
      A string representing the instruction description.
    """
    self._num_paragraphs = num_paragraphs
    if self._num_paragraphs is None or self._num_paragraphs < 0:
      self._num_paragraphs = random.randint(1, _NUM_PARAGRAPHS)

    self._description_pattern = (
        "There should be {num_paragraphs} paragraphs. " +
        "Paragraphs are separated with the markdown divider: ***")

    return self._description_pattern.format(num_paragraphs=self._num_paragraphs)

  def get_instruction_args(self):
    """Returns the keyward args of `build_description`."""
    return {"num_paragraphs": self._num_paragraphs}

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["num_paragraphs"]

  def check_following(self, value):
    """Checks the response contains required number of paragraphs.

    Args:
      value: A string representing the response. The response may contain
        paragraphs that are separated by the markdown divider: `***`.

    Returns:
      True if the actual number of paragraphs is the same as required;
      otherwise, False.
    """
    threshold = 5
    count = 0
    pos = 0

    while True:
      pos = value.find("***", pos)
      if pos == -1:
        break

      if pos >= threshold and pos <= len(value) - (3 + threshold):
        count += 1

      pos += 4  # advance by 3 for non-overlapping matches


    return count == self._num_paragraphs - 1 # since *** is a saparator, the actual count is 1 more than the number of separators

class PostscriptChecker(Instruction):
  """Checks the postscript."""

  def build_description(self, *, postscript_marker = None
                        ):
    """Build the instruction description.

    Args:
      postscript_marker: A string containing the keyword that marks the start
        of the postscript section.

    Returns:
      A string representing the instruction description.
    """
    self._postscript_marker = postscript_marker.strip() if isinstance(
        postscript_marker, str) else postscript_marker
    if self._postscript_marker is None:
      self._postscript_marker = random.choice(_POSTSCRIPT_MARKER)

    self._description_pattern = (
        "At the end of your response, please explicitly add a postscript " +
        "starting with {postscript}")

    return self._description_pattern.format(postscript=self._postscript_marker)

  def get_instruction_args(self):
    """Returns the keyward args of `build_description`."""
    return {"postscript_marker": self._postscript_marker}

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["postscript_marker"]

  def check_following(self, value):
    """Checks if the response follows the postscript format.

    Args:
      value: a string representing the response. The response is expected to
        contain a postscript section.

    Returns:
      True if the response contains a postscript section starting with
      the keyword containing in the `instruction_args`; otherwise False.
    """
    return self._postscript_marker.lower() in value.lower()

class KeywordChecker(Instruction):
  """Check the exisitence of certain keywords."""

  def build_description(self, *, keywords = None
                        ):
    """Build the instruction description.

    Args:
      keywords: A sequence of strings representing the keywords that are
        expected in the response.

    Returns:
      A string representing the instruction description.
    """

    if not keywords:
      self._keywords = instructions_util.generate_keywords(
          num_keywords=_NUM_KEYWORDS)
    else:
      self._keywords = keywords
    self._keywords = sorted(self._keywords)

    self._description_pattern = ("Include keywords {keywords} in the response.")

    return self._description_pattern.format(keywords=self._keywords)

  def get_instruction_args(self):
    """Returns the keyward args of `build_description`."""
    return {"keywords": self._keywords}

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["keywords"]


  def check_following(self, value):
    """Check if the response contain the expected keywords."""
    for keyword in self._keywords:
      if not instructions_util.contains_word(value, keyword):
        return False
    return True


class KeywordFrequencyChecker(Instruction):
  """Check the keyword frequency."""
  _stemmer = Stemmer.Stemmer("english")

  def build_description(self, *, keyword = None,
                        frequency = None,
                        relation = None):
    """Build the instruction description.

    Args:
      keyword: A string representing a keyword that is expected in the response.
      frequency: An integer specifying the number of times `keyword` is expected
        to appear in the response.
      relation: A string in (`less than`, `at least`), defining the relational
        operator for comparison.
        Two relational comparisons are supported for now:
        if 'less than', the actual number of occurrences < frequency;
        if 'at least', the actual number of occurrences >= frequency.

    Returns:
      A string representing the instruction description.
    """
    if not keyword:
      self._keyword = instructions_util.generate_keywords(num_keywords=1)[0]
    else:
      self._keyword = keyword.strip()

    self._frequency = frequency
    if self._frequency is None or self._frequency < 0:
      self._frequency = random.randint(1, _KEYWORD_FREQUENCY)

    if relation is None:
      self._comparison_relation = random.choice(_COMPARISON_RELATION)
    elif relation not in _COMPARISON_RELATION:
      raise ValueError("The supported relation for comparison must be in "
                       f"{_COMPARISON_RELATION}, but {relation} is given.")
    else:
      self._comparison_relation = relation

    self._description_pattern = (
        "In your response, the word {keyword} should appear {relation} " +
        "{frequency} times.")

    return self._description_pattern.format(
        keyword=self._keyword,
        relation=self._comparison_relation,
        frequency=self._frequency)

  def get_instruction_args(self):
    """Returns the keyward args of `build_description`."""
    return {"keyword": self._keyword,
            "frequency": self._frequency,
            "relation": self._comparison_relation}

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["keyword", "frequency", "relation"]

  def getStem(self, word: str) -> str:
    return self._stemmer.stemWord(word)

  def getIrregularPlural(self, word: str) -> str:
    return instructions_util.PLURALS.get(word, word)

  def getIrregularSingular(self, word: str) -> str:
    return instructions_util.SINGULARS.get(word, word)


  def CountKeywordOccurrences(self, text: str, keyword: str) -> int:
    count = 0
    hasStem = False
    stemSubstring = False
    hasPlural = False
    hasSingular = False

    keyword_base = keyword.lower()
    keyword_stem = self.getStem(keyword_base)
    keyword_plural = self.getIrregularPlural(keyword_base)
    keyword_singular = ""

    hasStem = keyword_stem != keyword_base
    stemSubstring = keyword_base.find(keyword_stem) != -1

    # if the irregular plural can be stemmed to the keyword or vice versa, it should be handled by the stemming logic
    hasPlural = keyword_plural != keyword and self.getStem(keyword_plural) != keyword_stem

    if not hasPlural:
        keyword_singular = self.getIrregularSingular(keyword_base)
        hasSingular = (
            keyword_singular != keyword_base
            and self.getStem(keyword_singular) != keyword_stem
        )

    search_keyword = keyword_stem if stemSubstring else keyword_base

    pos = 0
    found = ""

    # count keywords by matching the smallest possible substring of the keyword,
    # expanding it, and comparing to possible variants.
    while True:
        result = instructions_util.find_containing_word(text, search_keyword, pos)
        if result is None:
            break

        pos, found = result
        match = False
        found = found.lower()

        # Exact match, Hooray!
        if found == keyword_base:
            match = True

        foundStem = self.getStem(found)

        # stem match to original keyword (looking for "word", found "words" or "wording")
        if not match and foundStem == keyword_base:
            match = True

        if not match and hasStem and stemSubstring:
            # match to stemmed keyword (original keyword is "words", found "word")
            if found == keyword_stem:
                match = True
            # stem match to stemmed keyword (original keyword is "words", found "wording")
            elif foundStem != found and foundStem == keyword_stem:
                match = True

        if match:
            count += 1

        pos += len(found)

    # the stem's lettering differs from the original (words that end with 'y')
    if hasStem and not stemSubstring:
        pos = 0
        while True:
            result = instructions_util.find_containing_word(text, keyword_stem, pos)
            if result is None:
                break

            pos, found = result
            found = found.lower()

            # stem match to stemmed keyword
            # (original keyword is "try" (stemmed to "tri"), found "tries")
            # since this loop only runs if stem differs from the keyword,
            # we can safely assume no overlap occurs with the first loop.
            if self.getStem(found) == keyword_stem:
                count += 1

            pos += len(found)

    # count instances of irregular plurals not caught by the first loop
    # this assumes that the plural doesn't stem to the original word
    # (plural "children" isn't irregular since it stems to kw "child")
    if hasPlural:
        pos = 0
        while True:
            result = instructions_util.find_containing_word(text, keyword_plural, pos)
            if result is None:
                break

            pos, found = result
            found = found.lower()

            # match to pluralized keyword (original keyword is "mouse", found "mice")
            if found == keyword_plural:
                count += 1

            pos += len(found)
            # plural match to pluralized keyword is the same as an exact match.

    # count instances of irregular singulars not caught by the first loop
    # this assumes that the keyword doesn't stem to the singular
    # (kw "children" isn't irregular since it stems to singular "child")
    if hasSingular:
        pos = 0
        while True:
            result = instructions_util.find_containing_word(text, keyword_singular, pos)
            if result is None:
                break

            pos, found = result
            found = found.lower()

            # match to singular keyword (original keyword is "mice", found "mouse")
            if found == keyword_singular:
                count += 1

            pos += len(found)
            # singular match to singularized keyword is the same as an exact match.

    return count


  def check_following(self, value):
    """Checks if the response contain the keyword with required frequency."""
    actual_occurrences = self.CountKeywordOccurrences(value, self._keyword)

    if self._comparison_relation == _COMPARISON_RELATION[0]:
      return actual_occurrences < self._frequency
    elif self._comparison_relation == _COMPARISON_RELATION[1]:
      return actual_occurrences >= self._frequency  # pytype: disable=bad-return-type


class NumberOfWords(Instruction):
  """Checks the number of words."""

  def build_description(self, *, num_words = None,
                        relation = None):
    """Build the instruction description.

    Args:
      num_words: An integer specifying the number of words contained in the
        response.
      relation: A string in (`less than`, `at least`), defining the relational
        operator for comparison.
        Two relational comparisons are supported for now:
        if 'less than', the actual number of words < num_words;
        if 'at least', the actual number of words >= num_words.

    Returns:
      A string representing the instruction description.
    """

    self._num_words = num_words
    if self._num_words is None or self._num_words < 0:
      self._num_words = random.randint(
          _NUM_WORDS_LOWER_LIMIT, _NUM_WORDS_UPPER_LIMIT
      )

    if relation is None:
      self._comparison_relation = random.choice(_COMPARISON_RELATION)
    elif relation not in _COMPARISON_RELATION:
      raise ValueError("The supported relation for comparison must be in "
                       f"{_COMPARISON_RELATION}, but {relation} is given.")
    else:
      self._comparison_relation = relation

    self._description_pattern = (
        "Answer with {relation} {num_words} words.")

    return self._description_pattern.format(
        relation=self._comparison_relation,
        num_words=self._num_words)

  def get_instruction_args(self):
    """Returns the keyward args of `build_description`."""
    return {"num_words": self._num_words,
            "relation": self._comparison_relation}

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["num_words", "relation"]

  def check_following(self, value):
    """Checks if the response contains the expected number of words."""
    num_words = instructions_util.count_words(value)

    if self._comparison_relation == _COMPARISON_RELATION[0]:
      return num_words < self._num_words
    elif self._comparison_relation == _COMPARISON_RELATION[1]:
      return num_words >= self._num_words  # pytype: disable=bad-return-type


class JsonFormat(Instruction):
  """Check the Json format."""

  def build_description(self):
    self._description_pattern = (
        "Entire output should be wrapped in JSON format. You can use markdown"
        " ticks such as ```."
    )
    return self._description_pattern

  def get_instruction_args(self):
    """Returns the keyward args of `build_description`."""
    return None

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return []

  def check_following(self, value):
    t = value
    if not t:
      return False

    if t.startswith('`'):
      first, last = t.find('\n'), t.rfind('\n')
      if first != -1 and last > first:
        t = t[first + 1:last]
    try:
      json.loads(t)
    except ValueError as e:
      return False
    return True


class ParagraphFirstWordCheck(Instruction):
  """Check the paragraph and the first word of the nth paragraph."""

  def build_description(self, num_paragraphs = None,
                        nth_paragraph = None,
                        first_word = None):
    r"""Build the instruction description.

    Args:
      num_paragraphs: An integer indicating the number of paragraphs expected
        in the response. A paragraph is a subset of the string that is
        expected to be separated by '\n\n'.
      nth_paragraph: An integer indicating the paragraph number that we look at.
        Note that n starts from 1.
      first_word: A string that represent the first word of the bth paragraph.

    Returns:
      A string representing the instruction description.
    """
    self._num_paragraphs = num_paragraphs
    if self._num_paragraphs is None or self._num_paragraphs < 0:
      self._num_paragraphs = random.randint(1, _NUM_PARAGRAPHS)

    self._nth_paragraph = nth_paragraph
    if (
        self._nth_paragraph is None
        or self._nth_paragraph <= 0
        or self._nth_paragraph > self._num_paragraphs
    ):
      self._nth_paragraph = random.randint(1, self._num_paragraphs + 1)

    self._first_word = first_word
    if self._first_word is None:
      self._first_word = instructions_util.generate_keywords(num_keywords=1)[0]
    self._first_word = self._first_word.lower()

    self._description_pattern = (
        "There should be {num_paragraphs} paragraphs. " +
        "Paragraphs and only paragraphs are separated with each other by two " +
        "new lines as if it was '\\n\\n' in python. " +
        "Paragraph {nth_paragraph} must start with word {first_word}.")

    return self._description_pattern.format(
        num_paragraphs=self._num_paragraphs,
        nth_paragraph=self._nth_paragraph,
        first_word=self._first_word)

  def get_instruction_args(self):
    """Returns the keyward args of `build_description`."""
    return {"num_paragraphs": self._num_paragraphs,
            "nth_paragraph": self._nth_paragraph,
            "first_word": self._first_word}

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["num_paragraphs", "nth_paragraph", "first_word"]

  def FirstWord(self, s):
    # std::istringstream >> w : first whitespace-delimited token
    parts = s.split(None, 1)
    if not parts:
      w = ""
    else:
      w = parts[0]

    w = w.lower()

    fw = []
    for c in w:
      if c.isalpha() and not c.isspace():
        fw.append(c)

    return "".join(fw)


  def SplitParagraphs(self, s):
    # paragraphs separated only by the literal delimiter "\n\n"
    paras = []
    start = 0

    while True:
      pos = s.find("\n\n", start)
      if pos == -1:
        chunk = s[start:]
        if chunk != "":
          paras.append(chunk.rstrip())
        break

      chunk = s[start:pos]
      if chunk != "":
        paras.append(chunk.rstrip())

      start = pos + 2  # skip the delimiter

    return paras

  def check_following(self, value):
    """Checks for required number of paragraphs and correct first word.

    Args:
      value: a string representing the response. The response may contain
        paragraphs that are separated by two new lines and the first word of
        the nth paragraph will have to match a specified word.

    Returns:
      True if the number of paragraphs is the same as required and the first
      word of the specified paragraph is the same as required. Otherwise, false.
    """

    paras = self.SplitParagraphs(value)

    if int(len(paras)) != self._num_paragraphs:
      return False

    if self._nth_paragraph <= 0 or self._nth_paragraph > int(len(paras)):
      return False

    target = paras[self._nth_paragraph - 1].strip()
    if target == "":
      return False

    return self.FirstWord(target) == self._first_word.lower()


class ForbiddenWords(Instruction):
  """Checks that specified words are not used in response."""

  def build_description(self, forbidden_words = None
                        ):
    """Build the instruction description.

    Args:
      forbidden_words: A sequences of strings respresenting words that are not
        allowed in the response.

    Returns:
      A string representing the instruction description.
    """

    if not forbidden_words:
      self._forbidden_words = instructions_util.generate_keywords(
          num_keywords=_NUM_KEYWORDS)
    else:
      self._forbidden_words = list(set(forbidden_words))
    self._forbidden_words = sorted(self._forbidden_words)
    self._description_pattern = (
        "Do not include keywords {forbidden_words} in the response."
    )

    return self._description_pattern.format(
        forbidden_words=self._forbidden_words
    )

  def get_instruction_args(self):
    """Returns the keyward args of `build_description`."""
    return {"forbidden_words": self._forbidden_words}

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["forbidden_words"]

  def check_following(self, value):
    """Check if the response does not contain the expected keywords."""
    return instructions_util.contains_none(value, self._forbidden_words)




class TwoResponsesChecker(Instruction):
  """Check that two responses were given."""

  def build_description(self):
    """Build the instruction description."""
    self._description_pattern = (
        "Give two different responses. Responses and only responses should"
        " be separated by 6 asterisk symbols: ******."
    )
    return self._description_pattern

  def get_instruction_args(self):
    """Returns the keyward args of `build_description`."""
    return None

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return []

  def check_following(self, value):
    """Checks if the response has two different answers.

    Args:
      value: A string representing the response.

    Returns:
      True if two responses are detected and false otherwise.
    """
    count = 0
    pos = value.find("******")

    while pos != -1:
      if pos == 0 or pos == len(value) - 6:
        # ignore indicators at the start and end
        pos = value.find("******", pos + 6)
        continue

      count += 1
      if count > 1:
        return False  # more than one occurrence

      # disallow overlapping matches
      pos = value.find("******", pos + 6)

    return count > 0



class RepeatPromptThenAnswer(Instruction):
  """Checks that Prompt is first repeated then answered."""

  def build_description(self, *, prompt_to_repeat = None):
    """Build the instruction description.

    Args:
      prompt_to_repeat: The prompt that is meant to be repeated.

    Returns:
      A string representing the instruction description.
    """
    if not prompt_to_repeat:
      raise ValueError("prompt_to_repeat must be set.")
    else:
      self._prompt_to_repeat = prompt_to_repeat
    self._description_pattern = (
        "First repeat the request word for word without change,"
        " then give your answer (1. do not say any words or characters"
        " before repeating the request; 2. the request you need to repeat"
        " does not include this sentence)"
    )
    return self._description_pattern

  def get_instruction_args(self):
    return {"prompt_to_repeat": self._prompt_to_repeat}

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["prompt_to_repeat"]

  def check_following(self, value):
    return instructions_util.starts_with(value, self._prompt_to_repeat, 3);


class EndChecker(Instruction):
  """Checks that the prompt ends with a given phrase."""

  def build_description(self, *, end_phrase = None):
    """Build the instruction description.

    Args:
      end_phrase: A string representing the phrase the response should end with.

    Returns:
      A string representing the instruction description.
    """
    self._end_phrase = (
        end_phrase.strip() if isinstance(end_phrase, str) else end_phrase
    )
    if self._end_phrase is None:
      self._end_phrase = random.choice(_ENDING_OPTIONS)
    self._description_pattern = (
        "Finish your response with this exact phrase {ender}. "
        "No other words should follow this phrase.")
    return self._description_pattern.format(ender=self._end_phrase)

  def get_instruction_args(self):
    return {"end_phrase": self._end_phrase}

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["end_phrase"]

  def check_following(self, value):
    """Checks if the response ends with the expected phrase."""
    return instructions_util.ends_with(value, self._end_phrase, 3);


class TitleChecker(Instruction):
  """Checks the response for a title."""

  def build_description(self):
    """Build the instruction description."""
    self._description_pattern = (
        "Your answer must contain a title, wrapped in double angular brackets,"
        " such as <<poem of joy>>."
    )
    return self._description_pattern

  def get_instruction_args(self):
    return None

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return []

  def check_following(self, value):
    """Checks if the response contains a title."""
    i = value.find("<<")
    return i != -1 and ">>" in value[i + 2:]


class LetterFrequencyChecker(Instruction):
  """Checks letter frequency."""

  def build_description(self, *, letter = None,
                        let_frequency = None,
                        let_relation = None):
    """Build the instruction description.

    Args:
      letter: A string representing a letter that is expected in the response.
      let_frequency: An integer specifying the number of times `keyword` is
        expected to appear in the response.
      let_relation: A string in (`less than`, `at least`), defining the
        relational operator for comparison. Two relational comparisons are
        supported for now; if 'less than', the actual number of
        occurrences < frequency; if 'at least', the actual number of
        occurrences >= frequency.

    Returns:
      A string representing the instruction description.
    """
    if (
        not letter
        or len(letter) > 1
        or ord(letter.lower()) < 97
        or ord(letter.lower()) > 122
    ):
      self._letter = random.choice(list(string.ascii_letters))
    else:
      self._letter = letter.strip()
    self._letter = self._letter.lower()

    self._frequency = let_frequency
    if self._frequency is None or self._frequency < 0:
      self._frequency = random.randint(1, _LETTER_FREQUENCY)

    if let_relation is None:
      self._comparison_relation = random.choice(_COMPARISON_RELATION)
    elif let_relation not in _COMPARISON_RELATION:
      raise ValueError(
          "The supported relation for comparison must be in "
          f"{_COMPARISON_RELATION}, but {let_relation} is given."
      )
    else:
      self._comparison_relation = let_relation

    self._description_pattern = (
        "In your response, the letter {letter} should appear {let_relation}"
        " {let_frequency} times."
    )

    return self._description_pattern.format(
        letter=self._letter,
        let_frequency=self._frequency,
        let_relation=self._comparison_relation,
    )

  def get_instruction_args(self):
    """Returns the keyword args of build description."""
    return {"letter": self._letter,
            "let_frequency": self._frequency,
            "let_relation": self._comparison_relation}

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["letter", "let_frequency", "let_relation"]

  def check_following(self, value):
    """Checks that the response contains the letter at the right frequency."""
    logging.info(self._letter)
    count = value.lower().count(self._letter.lower())

    if self._comparison_relation == _COMPARISON_RELATION[0]:
      return count < self._frequency
    else:
      return count >= self._frequency


class CapitalLettersEnglishChecker(Instruction):
  """Checks that the response is in english and is in all capital letters."""

  def build_description(self):
    """Build the instruction description."""
    self._description_pattern = (
        "Your entire response should be in English, and in all capital letters."
    )
    return self._description_pattern

  def get_instruction_args(self):
    return None

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return []

  def check_following(self, value):
    """Checks that the response is in English and in all capital letters."""
    return all(not c.isalpha() or c.isupper() for c in value)



class LowercaseLettersEnglishChecker(Instruction):
  """Checks that the response is in english and is in all lowercase letters."""

  def build_description(self):
    """Build the instruction description."""
    self._description_pattern = (
        "Your entire response should be in English, and in all lowercase"
        " letters. No capital letters are allowed."
    )
    return self._description_pattern

  def get_instruction_args(self):
    return None

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return []

  def check_following(self, value):
    """Checks that the response is in English and in all lowercase letters."""
    return all(not c.isalpha() or c.islower() for c in value)


class CommaChecker(Instruction):
  """Checks the response for no commas."""

  def build_description(self):
    """Build the instruction description."""
    self._description_pattern = (
        "In your entire response, refrain from the use of any commas."
    )
    return self._description_pattern

  def get_instruction_args(self):
    return None

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return []

  def check_following(self, value):
    """Checks that the response does not contain commas."""
    return ',' not in value


class CapitalWordFrequencyChecker(Instruction):
  """Checks frequency of words with all capital letters."""

  def build_description(
      self,
      capital_frequency = None,
      capital_relation = None,
  ):
    """Build the instruction description.

    Args:
      capital_frequency: An integer that represents the number of words that
        should be in all capital letters.
      capital_relation: A string that is 'at least' or 'at most' that refers to
        the frequency.

    Returns:
      A string representing the instruction description.
    """
    self._frequency = capital_frequency
    if self._frequency is None:
      self._frequency = random.randint(1, _ALL_CAPITAL_WORD_FREQUENCY)

    self._comparison_relation = capital_relation
    if capital_relation is None:
      self._comparison_relation = random.choice(_COMPARISON_RELATION)
    elif capital_relation not in _COMPARISON_RELATION:
      raise ValueError(
          "The supported relation for comparison must be in "
          f"{_COMPARISON_RELATION}, but {capital_relation} is given."
      )

    self._description_pattern = (
        "In your response, words with all capital letters should appear"
        " {relation} {frequency} times."
    )

    return self._description_pattern.format(
        frequency=self._frequency, relation=self._comparison_relation
    )

  def get_instruction_args(self):
    """Returns the keyword args of build description."""
    return {
        "capital_frequency": self._frequency,
        "capital_relation": self._comparison_relation,
    }

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return ["capital_frequency", "capital_relation"]

  def is_all_caps_token(self, t: str) -> bool:
    # trim leading/trailing punctuation (keep '-' and '\'' because they appear inside words)
    def is_trim(c: str) -> bool:
      return not (c.isalnum() or c == '-' or c == "'")

    b = 0
    e = len(t)

    while b < e and is_trim(t[b]):
      b += 1
    while e > b and is_trim(t[e - 1]):
      e -= 1

    if b >= e:
      return False

    seen_alpha = False
    for i in range(b, e):
      c = t[i]
      if c.isalpha():
        seen_alpha = True
        if c.islower():
          return False  # any lowercase letter breaks ALL-CAPS
      # digits, '-', '\'' are allowed and ignored for casing

    return seen_alpha  # at least one letter, and no lowercase letters


  def count_all_caps_words(self, resp: str) -> int:
    count = 0
    for tok in resp.split():
      if self.is_all_caps_token(tok):
        count += 1
    return count

  def check_following(self, value):
    """Checks the frequency of words with all capital letters."""
    capital_words = self.count_all_caps_words(value)

    if self._comparison_relation == _COMPARISON_RELATION[0]:
      return capital_words < self._frequency
    else:
      return capital_words >= self._frequency


class QuotationChecker(Instruction):
  """Checks response is wrapped with double quotation marks."""

  def build_description(self):
    """Build the instruction description."""
    self._description_pattern = (
        "Wrap your entire response with double quotation marks."
    )
    return self._description_pattern

  def get_instruction_args(self):
    """Returns the keyword args of build description."""
    return None

  def get_instruction_args_keys(self):
    """Returns the args keys of `build_description`."""
    return []

  def check_following(self, value):
    """Checks if the response is wrapped with double quotation marks."""
    value = value.strip()
    return len(value) > 1 and value[0] == '"' and value[-1] == '"'
