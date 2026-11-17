"""Compiling grammar for efficient token mask generation."""

from typing import Any, Dict, List, Optional, Tuple, Type, Union, overload

from pydantic import BaseModel

from .base import XGRObject, _core
from .grammar import (
    Grammar,
    StructuralTagItem,
    _convert_named_grammars_to_lists,
    _convert_schema_to_str,
    _get_structural_tag_str_from_args,
)
from .structural_tag import StructuralTag
from .tokenizer_info import TokenizerInfo


class CompiledGrammar(XGRObject):
    """This is the primary object to store compiled grammar.

    A CompiledGrammar can be used to construct GrammarMatcher
    to generate token masks efficiently.

    Notes
    -----
    Do not construct this class directly, instead
    use :class:`GrammarCompiler` to construct the object.
    """

    @property
    def grammar(self) -> Grammar:
        """The original grammar."""
        return Grammar._create_from_handle(self._handle.grammar())

    @property
    def tokenizer_info(self) -> TokenizerInfo:
        """The tokenizer info associated with the compiled grammar."""
        return TokenizerInfo._create_from_handle(self._handle.tokenizer_info())

    @property
    def memory_size_bytes(self) -> int:
        """The approximate memory usage of the compiled grammar in bytes."""
        return self._handle.memory_size_bytes()

    def serialize_json(self) -> str:
        """Serialize the compiled grammar to a JSON string. It will serialize the compiled grammar
        without the tokenizer info, since the tokenizer info is shared by multiple compiled
        grammars.

        Notes
        -----
        The metadata of the tokenizer info is serialized and will be checked when deserializing.

        Returns
        -------
        json_string : str
            The JSON string.
        """
        return str(self._handle.serialize_json())

    @staticmethod
    def deserialize_json(json_str: str, tokenizer_info: TokenizerInfo) -> "CompiledGrammar":
        """Deserialize the compiled grammar from a JSON string and associate it with the specified
        tokenizer info.

        Notes
        -----
        This will check the metadata of the tokenizer info matching the serialized metadata in
        json_str. If the metadata does not match, a DeserializeFormatError will be raised.

        Parameters
        ----------
        json_str : str
            The JSON string.

        tokenizer_info : TokenizerInfo
            The tokenizer info.

        Returns
        -------
        compiled_grammar : CompiledGrammar
            The compiled grammar.

        Raises
        ------
        InvalidJSONError
            When the JSON string is invalid.
        DeserializeFormatError
            When the JSON string does not follow the serialization format of the grammar, or the
            tokenizer info metadata does not match.
        DeserializeVersionError
            When the __VERSION__ field in the JSON string is not the same as the current version.
        """
        return CompiledGrammar._create_from_handle(
            _core.CompiledGrammar.deserialize_json(json_str, tokenizer_info._handle)
        )


class GrammarCompiler(XGRObject):
    """The compiler for grammars. It is associated with a certain tokenizer info, and compiles
    grammars into CompiledGrammar with the tokenizer info. It allows parallel compilation with
    multiple threads, and has a cache to store the compilation result, avoiding compiling the
    same grammar multiple times.
    """

    def __init__(
        self,
        tokenizer_info: TokenizerInfo,
        *,
        max_threads: int = 8,
        cache_enabled: bool = True,
        cache_limit_bytes: int = -1,
    ):
        """Construct the compiler.

        Parameters
        ----------
        tokenizer_info : TokenizerInfo
            The tokenizer info.

        max_threads : int, default: 8
            The maximum number of threads used to compile the grammar.

        cache_enabled : bool, default: True
            Whether to enable the cache.

        cache_limit_bytes : int, default: -1
            The maximum memory usage for the cache in the specified unit.
            Note that the actual memory usage may slightly exceed this value.
        """
        if not isinstance(tokenizer_info, TokenizerInfo):
            raise ValueError(
                "Please convert the tokenizer to TokenizerInfo before passing it "
                "to GrammarCompiler."
            )

        self._init_handle(
            _core.GrammarCompiler(
                tokenizer_info._handle, max_threads, cache_enabled, cache_limit_bytes
            )
        )

    def compile_json_schema(
        self,
        schema: Union[str, Type[BaseModel], Dict[str, Any]],
        *,
        any_whitespace: bool = True,
        indent: Optional[int] = None,
        separators: Optional[Tuple[str, str]] = None,
        strict_mode: bool = True,
        max_whitespace_cnt: Optional[int] = None,
        any_order: bool = False,
    ) -> CompiledGrammar:
        """Get CompiledGrammar from the specified JSON schema and format. The indent
        and separators parameters follow the same convention as in json.dumps().

        Parameters
        ----------
        schema : Union[str, Type[BaseModel], Dict[str, Any]]
            The schema string or Pydantic model or JSON schema dict.

        indent : Optional[int], default: None
            The number of spaces for indentation. If None, the output will be in one line.

        separators : Optional[Tuple[str, str]], default: None
            Two separators used in the schema: comma and colon. Examples: (",", ":"), (", ", ": ").
            If None, the default separators will be used: (",", ": ") when the indent is not None,
            and (", ", ": ") otherwise.

        strict_mode : bool, default: True
            Whether to use strict mode. In strict mode, the generated grammar will not allow
            properties and items that is not specified in the schema. This is equivalent to
            setting unevaluatedProperties and unevaluatedItems to false.

            This helps LLM to generate accurate output in the grammar-guided generation with JSON
            schema.

        max_whitespace_cnt : Optional[int], default: None
            The maximum number of whitespace characters allowed between elements, such like keys, values, separators and so on.
            If None, there is no limit on the number of whitespace characters.
            If specified, it will limit the number of whitespace characters to at most max_whitespace_cnt.
            It should be a positive integer.

        any_order : bool, default: False
            Whether object properties may appear in any order.

            - False: properties follow the schema's declared order, fully validated (required
              keys present, no duplicates).
            - True: properties may appear in any order; only key validity and each key's value
              schema are enforced. Key presence and uniqueness are not checked, so required keys
              may be missing and keys may repeat. The entry count is bounded to
              ``[max(minProperties, n_required), maxProperties]`` (unbounded when maxProperties
              is unset).

            Applies to every object, nested included.

        Returns
        -------
        compiled_grammar : CompiledGrammar
            The compiled grammar.
        """
        schema_str = _convert_schema_to_str(schema)
        return CompiledGrammar._create_from_handle(
            self._handle.compile_json_schema(
                schema_str,
                any_whitespace,
                indent,
                separators,
                strict_mode,
                max_whitespace_cnt,
                any_order,
            )
        )

    def compile_builtin_json_grammar(self) -> CompiledGrammar:
        """Get CompiledGrammar from the standard JSON.

        Returns
        -------
        compiled_grammar : CompiledGrammar
            The compiled grammar.
        """
        return CompiledGrammar._create_from_handle(self._handle.compile_builtin_json_grammar())

    def compile_regex(self, regex: str) -> CompiledGrammar:
        """Get CompiledGrammar from the specified regex.

        Parameters
        ----------
        regex : str
            The regex string.

        Returns
        -------
        compiled_grammar : CompiledGrammar
            The compiled grammar.
        """
        return CompiledGrammar._create_from_handle(self._handle.compile_regex(regex))

    def compile_lark(
        self, lark_string: str, *, named_grammars: Optional[Dict[str, Union[Grammar, str]]] = None
    ) -> CompiledGrammar:
        """Compile a grammar from Lark syntax.

        The compiler's tokenizer info is used to resolve named special tokens.
        When caching is enabled, repeated calls with the same Lark source and
        named grammars reuse the previous compiled result.

        Parameters
        ----------
        lark_string : str
            The Lark grammar source. It must define a ``start`` rule.

        named_grammars : Optional[Dict[str, Union[Grammar, str]]]
            Grammars referenced by ``@name`` in the Lark source. String values
            contain Lark grammar sources with their own ``start`` rules. Dictionary
            keys are passed without the leading ``@``.

        Returns
        -------
        compiled_grammar : CompiledGrammar
            The compiled Lark grammar.
        """
        names, values = _convert_named_grammars_to_lists(named_grammars)
        return CompiledGrammar._create_from_handle(
            self._handle.compile_lark(lark_string, names, values)
        )

    @overload
    def compile_structural_tag(
        self, structural_tag: Union[StructuralTag, str, Dict[str, Any]]
    ) -> CompiledGrammar: ...

    @overload
    def compile_structural_tag(
        self, tags: List[StructuralTagItem], triggers: List[str]
    ) -> CompiledGrammar: ...

    def compile_structural_tag(self, *args, **kwargs) -> CompiledGrammar:
        """Compile a grammar from a structural tag. See the Structural Tag Usage in XGrammar
        documentation for its usage.

        This method supports two calling patterns:

        1. Single structural tag parameter:
           compile_structural_tag(structural_tag)

        2. Legacy pattern (deprecated):
           compile_structural_tag(tags, triggers)

        Parameters
        ----------
        structural_tag : Union[StructuralTag, str, Dict[str, Any]]
            The structural tag either as a StructuralTag object, or a JSON string or a dictionary.

        tags : List[StructuralTagItem]
            (Deprecated) The structural tags. Use StructuralTag class instead.

        triggers : List[str]
            (Deprecated) The triggers. Use StructuralTag class instead.

        Returns
        -------
        compiled_grammar : CompiledGrammar
            The compiled grammar from the structural tag.

        Raises
        ------
        InvalidJSONError
            When the structural tag is not a valid JSON string.
        InvalidStructuralTagError
            When the structural tag is not valid.
        TypeError
            When the arguments are invalid.

        Notes
        -----
        The legacy pattern compile_structural_tag(tags, triggers) is deprecated. Use the
        StructuralTag class to construct structural tags instead.
        """
        structural_tag_str = _get_structural_tag_str_from_args(args, kwargs)
        return CompiledGrammar._create_from_handle(
            self._handle.compile_structural_tag(structural_tag_str)
        )

    @overload
    def compile_grammar(
        self, ebnf_string: str, *, root_rule_name: str = "root"
    ) -> CompiledGrammar: ...

    @overload
    def compile_grammar(self, grammar: Grammar) -> CompiledGrammar: ...

    def compile_grammar(
        self, grammar: Union[str, Grammar], *, root_rule_name: str = "root"
    ) -> CompiledGrammar:
        """Compile a grammar object.

        Overloads:

        1. ``compile_grammar(ebnf_string: str, *, root_rule_name: str = "root") -> CompiledGrammar``
            - Compile a grammar from an EBNF string. The string should follow the format described
              in https://github.com/ggerganov/llama.cpp/blob/master/grammars/README.md.

        2. ``compile_grammar(grammar: Grammar) -> CompiledGrammar``
            - Compile a grammar from a Grammar object.

        Parameters
        ----------
        ebnf_string : str
            The grammar string in EBNF format.
        root_rule_name : str, default: "root"
            The name of the root rule in the grammar.
        grammar : Union[str, Grammar]
            The grammar string or Grammar object.

        Returns
        -------
        compiled_grammar : CompiledGrammar
            The compiled grammar.
        """
        if isinstance(grammar, str):
            return CompiledGrammar._create_from_handle(
                self._handle.compile_grammar_from_strings(grammar, root_rule_name)
            )
        elif isinstance(grammar, Grammar):
            return CompiledGrammar._create_from_handle(
                self._handle.compile_grammar_ebnf(grammar._handle)
            )
        else:
            raise ValueError("Invalid grammar type. Please pass a string or a Grammar object.")

    def prepare_byte_grammar(
        self,
        grammar: Grammar,
        *,
        max_observer_nodes: int = 2000000,
        experimental_unproven: bool = False,
    ) -> str:
        """Export optimized Grammar and native scheme-3 compilation sidecars as JSON.

        This trusted preprocessing creates no final mask cache. The caller translates
        byte fields and binds the result to an immutable model/profile. Cache must be
        disabled, and max_threads must be in [1, 8]. Preparation itself stays
        sequential; the thread setting does not change its serialized output.
        experimental_unproven explicitly relaxes only the proved feature-scope
        gates for offline comparisons. It does not repair unsupported semantics
        or weaken structural/plan validation, and is never a serving default.
        """
        if not isinstance(grammar, Grammar):
            raise TypeError("prepare_byte_grammar requires a Grammar object")
        if type(max_observer_nodes) is not int or not 1 <= max_observer_nodes <= 10000000:
            raise ValueError("max_observer_nodes must be in [1, 10000000]")
        if type(experimental_unproven) is not bool:
            raise TypeError("experimental_unproven must be a bool")
        return str(
            self._handle.prepare_byte_grammar(
                grammar._handle, max_observer_nodes, experimental_unproven
            )
        )

    def compile_optimized_byte_grammar(
        self,
        grammar: Grammar,
        plan_json: str,
        *,
        experimental_unproven: bool = False,
    ) -> CompiledGrammar:
        """Build native caches without reoptimizing a translated, sanitized Grammar.

        Sidecars supply reference intervals, the permuted ASCII predicate and finite
        native lookahead observations; they are not precomputed final token masks.
        With max_threads in [1, 8], state tasks may run in
        parallel over the same immutable inputs. The returned cache is complete;
        worker exceptions are propagated before a result can be returned.
        cache_enabled=True reuses exact final state masks across compatible
        rules, after validating each new plan. Native AST/base-mask lookahead
        adaptation is deliberately bypassed for this byte-only entry.
        experimental_unproven requires cache_enabled=False: the production
        exact key is not proved to include every newly admitted semantic input.
        This mode makes a real compilation attempt, not a support guarantee.
        """
        if not isinstance(grammar, Grammar) or not isinstance(plan_json, str):
            raise TypeError("expected a Grammar and byte-plan JSON string")
        if type(experimental_unproven) is not bool:
            raise TypeError("experimental_unproven must be a bool")
        return CompiledGrammar._create_from_handle(
            self._handle.compile_optimized_byte_grammar(
                grammar._handle, plan_json, experimental_unproven
            )
        )

    def inspect_compilation(
        self,
        grammar: Grammar,
        plan_json: Optional[str] = None,
        *,
        experimental_unproven: bool = False,
    ) -> str:
        """Test-only actual compilation and index/branch trace, without token text.

        None selects native optimization and cache construction. A plan selects the
        optimized byte entry. Production compile calls collect no diagnostic trace.
        This diagnostic entry requires cache disabled and max_threads in [1, 8].
        Traces are published safely and sorted by rule/state after all workers join,
        so thread completion order is not part of the comparison contract.
        experimental_unproven affects only an explicitly supplied byte plan;
        the native reference compilation with plan_json=None remains unchanged.
        """
        if not isinstance(grammar, Grammar) or (
            plan_json is not None and not isinstance(plan_json, str)
        ):
            raise TypeError("expected a Grammar and optional byte-plan JSON string")
        if plan_json is not None and not plan_json.strip():
            raise ValueError("an explicit byte plan must not be empty")
        if type(experimental_unproven) is not bool:
            raise TypeError("experimental_unproven must be a bool")
        return str(
            self._handle.inspect_compilation(
                grammar._handle, plan_json or "", experimental_unproven
            )
        )

    def clear_cache(self) -> None:
        """Clear all cached compiled grammars."""
        self._handle.clear_cache()

    def get_byte_cache_stats(self) -> str:
        """Return actual scheme-3 cache counters and accounted memory as JSON.

        This opt-in diagnostic is not needed on production cache hits. clear_cache
        resets both entries and counters. Memory includes persistent key bytes;
        temporary compiler/key-building allocations are outside the LRU budget.
        """
        return str(self._handle.get_byte_cache_stats())

    def get_cache_size_bytes(self) -> int:
        """The approximate memory usage of the cache in bytes."""
        return self._handle.get_cache_size_bytes()

    @property
    def cache_limit_bytes(self) -> int:
        """
        The maximum memory usage for the cache in bytes.
        Returns -1 if the cache has no memory limit.
        """
        return self._handle.cache_limit_bytes()
