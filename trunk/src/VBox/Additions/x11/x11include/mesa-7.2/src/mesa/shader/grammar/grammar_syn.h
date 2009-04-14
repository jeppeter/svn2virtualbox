".syntax grammar;\n"
".emtcode DECLARATION_END 0\n"
".emtcode DECLARATION_EMITCODE 1\n"
".emtcode DECLARATION_ERRORTEXT 2\n"
".emtcode DECLARATION_REGBYTE 3\n"
".emtcode DECLARATION_LEXER 4\n"
".emtcode DECLARATION_RULE 5\n"
".emtcode SPECIFIER_END 0\n"
".emtcode SPECIFIER_AND_TAG 1\n"
".emtcode SPECIFIER_OR_TAG 2\n"
".emtcode SPECIFIER_CHARACTER_RANGE 3\n"
".emtcode SPECIFIER_CHARACTER 4\n"
".emtcode SPECIFIER_STRING 5\n"
".emtcode SPECIFIER_IDENTIFIER 6\n"
".emtcode SPECIFIER_TRUE 7\n"
".emtcode SPECIFIER_FALSE 8\n"
".emtcode SPECIFIER_DEBUG 9\n"
".emtcode IDENTIFIER_SIMPLE 0\n"
".emtcode IDENTIFIER_LOOP 1\n"
".emtcode ERROR_NOT_PRESENT 0\n"
".emtcode ERROR_PRESENT 1\n"
".emtcode EMIT_NULL 0\n"
".emtcode EMIT_INTEGER 1\n"
".emtcode EMIT_IDENTIFIER 2\n"
".emtcode EMIT_CHARACTER 3\n"
".emtcode EMIT_LAST_CHARACTER 4\n"
".emtcode EMIT_CURRENT_POSITION 5\n"
".errtext INVALID_GRAMMAR \"internal error 2001: invalid grammar script\"\n"
".errtext SYNTAX_EXPECTED \"internal error 2002: '.syntax' keyword expected\"\n"
".errtext IDENTIFIER_EXPECTED \"internal error 2003: identifier expected\"\n"
".errtext MISSING_SEMICOLON \"internal error 2004: missing ';'\"\n"
".errtext INTEGER_EXPECTED \"internal error 2005: integer value expected\"\n"
".errtext STRING_EXPECTED \"internal error 2006: string expected\"\n"
"grammar\n"
" grammar_1 .error INVALID_GRAMMAR;\n"
"grammar_1\n"
" optional_space .and \".syntax\" .error SYNTAX_EXPECTED .and space .and identifier .and\n"
" semicolon .and declaration_list .and optional_space .and '\\0' .emit DECLARATION_END;\n"
"optional_space\n"
" space .or .true;\n"
"space\n"
" single_space .and .loop single_space;\n"
"single_space\n"
" white_char .or comment_block;\n"
"white_char\n"
" ' ' .or '\\t' .or '\\n' .or '\\r';\n"
"comment_block\n"
" '/' .and '*' .and comment_rest;\n"
"comment_rest\n"
" .loop comment_char_no_star .and comment_rest_1;\n"
"comment_rest_1\n"
" comment_end .or comment_rest_2;\n"
"comment_rest_2\n"
" '*' .and comment_rest;\n"
"comment_char_no_star\n"
" '\\x2B'-'\\xFF' .or '\\x01'-'\\x29';\n"
"comment_end\n"
" '*' .and '/';\n"
"identifier\n"
" identifier_ne .error IDENTIFIER_EXPECTED;\n"
"identifier_ne\n"
" first_idchar .emit * .and .loop follow_idchar .emit * .and .true .emit '\\0';\n"
"first_idchar\n"
" 'a'-'z' .or 'A'-'Z' .or '_';\n"
"follow_idchar\n"
" first_idchar .or digit_dec;\n"
"digit_dec\n"
" '0'-'9';\n"
"semicolon\n"
" optional_space .and ';' .error MISSING_SEMICOLON .and optional_space;\n"
"declaration_list\n"
" declaration .and .loop declaration;\n"
"declaration\n"
" emitcode_definition .emit DECLARATION_EMITCODE .or\n"
" errortext_definition .emit DECLARATION_ERRORTEXT .or\n"
" regbyte_definition .emit DECLARATION_REGBYTE .or\n"
" lexer_definition .emit DECLARATION_LEXER .or\n"
" rule_definition .emit DECLARATION_RULE;\n"
"emitcode_definition\n"
" \".emtcode\" .and space .and identifier .and space .and integer .and space_or_null;\n"
"integer\n"
" integer_ne .error INTEGER_EXPECTED;\n"
"integer_ne\n"
" hex_integer .emit 0x10 .or dec_integer .emit 10;\n"
"hex_integer\n"
" hex_prefix .and digit_hex .emit * .and .loop digit_hex .emit * .and .true .emit '\\0';\n"
"hex_prefix\n"
" '0' .and hex_prefix_1;\n"
"hex_prefix_1\n"
" 'x' .or 'X';\n"
"digit_hex\n"
" '0'-'9' .or 'a'-'f' .or 'A'-'F';\n"
"dec_integer\n"
" digit_dec .emit * .and .loop digit_dec .emit * .and .true .emit '\\0';\n"
"space_or_null\n"
" space .or '\\0';\n"
"errortext_definition\n"
" \".errtext\" .and space .and identifier .and space .and string .and space_or_null;\n"
"string\n"
" string_ne .error STRING_EXPECTED;\n"
"string_ne\n"
" '\"' .and .loop string_char_double_quotes .and '\"' .emit '\\0';\n"
"string_char_double_quotes\n"
" escape_sequence .or string_char .emit * .or '\\'' .emit *;\n"
"string_char\n"
" '\\x5D'-'\\xFF' .or '\\x28'-'\\x5B' .or '\\x23'-'\\x26' .or '\\x0E'-'\\x21' .or '\\x0B'-'\\x0C' .or\n"
" '\\x01'-'\\x09';\n"
"escape_sequence\n"
" '\\\\' .emit * .and escape_code;\n"
"escape_code\n"
" simple_escape_code .emit * .or hex_escape_code .or oct_escape_code;\n"
"simple_escape_code\n"
" '\\'' .or '\"' .or '?' .or '\\\\' .or 'a' .or 'b' .or 'f' .or 'n' .or 'r' .or 't' .or 'v';\n"
"hex_escape_code\n"
" 'x' .emit * .and digit_hex .emit * .and .loop digit_hex .emit *;\n"
"oct_escape_code\n"
" digit_oct .emit * .and optional_digit_oct .and optional_digit_oct;\n"
"digit_oct\n"
" '0'-'7';\n"
"optional_digit_oct\n"
" digit_oct .emit * .or .true;\n"
"regbyte_definition\n"
" \".regbyte\" .and space .and identifier .and space .and integer .and space_or_null;\n"
"lexer_definition\n"
" \".string\" .and space .and identifier .and semicolon;\n"
"rule_definition\n"
" identifier_ne .and space .and definition;\n"
"definition\n"
" specifier .and optional_specifiers_and_or .and semicolon .emit SPECIFIER_END;\n"
"optional_specifiers_and_or\n"
" and_specifiers .emit SPECIFIER_AND_TAG .or or_specifiers .emit SPECIFIER_OR_TAG .or .true;\n"
"specifier\n"
" specifier_condition .and optional_space .and specifier_rule;\n"
"specifier_condition\n"
" specifier_condition_1 .or .true;\n"
"specifier_condition_1\n"
" \".if\" .and optional_space .and '(' .and optional_space .and left_operand .and operator .and\n"
" right_operand .and optional_space .and ')';\n"
"left_operand\n"
" identifier;\n"
"operator\n"
" operator_1 .or operator_2;\n"
"operator_1\n"
" optional_space .and '!' .and '=' .and optional_space;\n"
"operator_2\n"
" optional_space .and '=' .and '=' .and optional_space;\n"
"right_operand\n"
" integer;\n"
"specifier_rule\n"
" specifier_rule_1 .and optional_error .and .loop emit .and .true .emit EMIT_NULL;\n"
"specifier_rule_1\n"
" character_range .emit SPECIFIER_CHARACTER_RANGE .or\n"
" character .emit SPECIFIER_CHARACTER .or\n"
" string_ne .emit SPECIFIER_STRING .or\n"
" \".true\" .emit SPECIFIER_TRUE .or\n"
" \".false\" .emit SPECIFIER_FALSE .or\n"
" \".debug\" .emit SPECIFIER_DEBUG .or\n"
" advanced_identifier .emit SPECIFIER_IDENTIFIER;\n"
"character\n"
" '\\'' .and string_char_single_quotes .and '\\'' .emit '\\0';\n"
"string_char_single_quotes\n"
" escape_sequence .or string_char .emit * .or '\"' .emit *;\n"
"character_range\n"
" character .and optional_space .and '-' .and optional_space .and character;\n"
"advanced_identifier\n"
" optional_loop .and identifier;\n"
"optional_loop\n"
" optional_loop_1 .emit IDENTIFIER_LOOP .or .true .emit IDENTIFIER_SIMPLE;\n"
"optional_loop_1\n"
" \".loop\" .and space;\n"
"optional_error\n"
" error .emit ERROR_PRESENT .or .true .emit ERROR_NOT_PRESENT;\n"
"error\n"
" space .and \".error\" .and space .and identifier;\n"
"emit\n"
" emit_output .or emit_regbyte;\n"
"emit_output\n"
" space .and \".emit\" .and space .and emit_param;\n"
"emit_param\n"
" integer_ne .emit EMIT_INTEGER .or\n"
" identifier_ne .emit EMIT_IDENTIFIER .or\n"
" character .emit EMIT_CHARACTER .or\n"
" '*' .emit EMIT_LAST_CHARACTER .or\n"
" '$' .emit EMIT_CURRENT_POSITION;\n"
"emit_regbyte\n"
" space .and \".load\" .and space .and identifier .and space .and emit_param;\n"
"and_specifiers\n"
" and_specifier .and .loop and_specifier;\n"
"or_specifiers\n"
" or_specifier .and .loop or_specifier;\n"
"and_specifier\n"
" space .and \".and\" .and space .and specifier;\n"
"or_specifier\n"
" space .and \".or\" .and space .and specifier;\n"
".string __string_filter;\n"
"__string_filter\n"
" __first_identifier_char .and .loop __next_identifier_char;\n"
"__first_identifier_char\n"
" 'a'-'z' .or 'A'-'Z' .or '_' .or '.';\n"
"__next_identifier_char\n"
" 'a'-'z' .or 'A'-'Z' .or '_' .or '0'-'9';\n"
""
