//! `.ptpn` domain language parser (port of `src/parser/ptpn_parser.cpp`).

use crate::petri::{PTPN, TimeInterval};
use std::collections::HashMap;

#[derive(Debug, Clone, Default)]
pub struct PlaceNode {
    pub id: String,
    pub name: String,
    pub capacity: i32,
}

#[derive(Debug, Clone)]
pub struct TransitionNode {
    pub id: String,
    pub name: String,
    pub time_min: i32,
    pub time_max: i32,
    pub left_open: bool,
    pub right_open: bool,
    pub priority: i32,
    pub core: i32,
    pub suspendable: bool,
}

impl Default for TransitionNode {
    fn default() -> Self {
        TransitionNode {
            id: String::new(),
            name: String::new(),
            time_min: 0,
            time_max: 0,
            left_open: false,
            right_open: false,
            priority: 0,
            core: -1,
            suspendable: false,
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct ArcNode {
    pub source: String,
    pub target: String,
    pub weight: i32,
}

#[derive(Debug, Clone, Default)]
pub struct InitNode {
    pub place: String,
    pub tokens: i32,
}

#[derive(Debug, Clone, Default)]
pub struct PTPNAST {
    pub places: Vec<PlaceNode>,
    pub transitions: Vec<TransitionNode>,
    pub arcs: Vec<ArcNode>,
    pub initial_marking: Vec<InitNode>,
}

fn is_whitespace(c: char) -> bool {
    c == ' ' || c == '\t' || c == '\n' || c == '\r'
}

fn skip_whitespace_and_comments(input: &str, pos: &mut usize) {
    let bytes: Vec<char> = input.chars().collect();
    loop {
        while *pos < bytes.len() && is_whitespace(bytes[*pos]) {
            *pos += 1;
        }
        if *pos + 1 < bytes.len() && bytes[*pos] == '/' && bytes[*pos + 1] == '/' {
            while *pos < bytes.len() && bytes[*pos] != '\n' {
                *pos += 1;
            }
            continue;
        }
        if *pos + 1 < bytes.len() && bytes[*pos] == '/' && bytes[*pos + 1] == '*' {
            *pos += 2;
            while *pos + 1 < bytes.len() && !(bytes[*pos] == '*' && bytes[*pos + 1] == '/') {
                *pos += 1;
            }
            if *pos < bytes.len() {
                *pos += 2;
            }
            continue;
        }
        break;
    }
}

fn read_identifier(input: &str, pos: &mut usize) -> String {
    let bytes: Vec<char> = input.chars().collect();
    let mut result = String::new();
    while *pos < bytes.len() && (bytes[*pos].is_alphanumeric() || bytes[*pos] == '_') {
        result.push(bytes[*pos]);
        *pos += 1;
    }
    result
}

fn read_number(input: &str, pos: &mut usize) -> i32 {
    let bytes: Vec<char> = input.chars().collect();
    let mut result = 0;
    while *pos < bytes.len() && bytes[*pos].is_digit(10) {
        result = result * 10 + (bytes[*pos] as u8 - b'0') as i32;
        *pos += 1;
    }
    result
}

fn read_signed_number(input: &str, pos: &mut usize) -> i32 {
    let bytes: Vec<char> = input.chars().collect();
    let mut negative = false;
    if *pos < bytes.len() && bytes[*pos] == '-' {
        negative = true;
        *pos += 1;
    } else if *pos < bytes.len() && bytes[*pos] == '+' {
        *pos += 1;
    }
    let value = read_number(input, pos);
    if negative {
        -value
    } else {
        value
    }
}

fn parse_transition_attribute(
    trans: &mut TransitionNode,
    input: &str,
    pos: &mut usize,
) -> bool {
    skip_whitespace_and_comments(input, pos);
    let bytes: Vec<char> = input.chars().collect();
    if *pos >= bytes.len() || bytes[*pos] != '@' {
        return false;
    }
    *pos += 1;
    skip_whitespace_and_comments(input, pos);

    if *pos < bytes.len() && (bytes[*pos].is_digit(10) || bytes[*pos] == '-') {
        trans.priority = read_signed_number(input, pos);
        return true;
    }

    let attr = read_identifier(input, pos);
    if attr == "priority" || attr == "core" || attr == "capacity" {
        skip_whitespace_and_comments(input, pos);
        if *pos < bytes.len() && bytes[*pos] == '=' {
            *pos += 1;
        }
        let val = read_signed_number(input, pos);
        if attr == "priority" {
            trans.priority = val;
        } else if attr == "core" {
            trans.core = val;
        }
        return true;
    }

    false
}

fn parse_bare_transition_attribute(
    trans: &mut TransitionNode,
    input: &str,
    pos: &mut usize,
) -> bool {
    let bytes: Vec<char> = input.chars().collect();
    if *pos >= bytes.len() || !bytes[*pos].is_alphabetic() {
        return false;
    }

    let word_start = *pos;
    let mut word = String::new();
    while *pos < bytes.len() && bytes[*pos].is_alphabetic() {
        word.push(bytes[*pos]);
        *pos += 1;
    }

    if word == "suspendable" {
        trans.suspendable = true;
        return true;
    }

    if word == "core" || word == "priority" {
        skip_whitespace_and_comments(input, pos);
        if *pos < bytes.len() && bytes[*pos] == '=' {
            *pos += 1;
        }
        let val = read_signed_number(input, pos);
        if word == "priority" {
            trans.priority = val;
        } else {
            trans.core = val;
        }
        return true;
    }

    *pos = word_start;
    false
}

pub struct PTPNParser;

impl PTPNParser {
    pub fn validate(ast: &PTPNAST, error: &mut String) -> bool {
        let mut place_ids: std::collections::HashSet<String> = std::collections::HashSet::new();
        for place in &ast.places {
            if place.id.is_empty() {
                *error = "Place definition has empty id".to_string();
                return false;
            }
            if !place_ids.insert(place.id.clone()) {
                *error = format!("Duplicate place id: {}", place.id);
                return false;
            }
            if place.capacity < 0 {
                *error = format!("Negative capacity for place: {}", place.id);
                return false;
            }
        }

        let mut transition_ids: std::collections::HashSet<String> =
            std::collections::HashSet::new();
        for trans in &ast.transitions {
            if trans.id.is_empty() {
                *error = "Transition definition has empty id".to_string();
                return false;
            }
            if !transition_ids.insert(trans.id.clone()) {
                *error = format!("Duplicate transition id: {}", trans.id);
                return false;
            }
            if trans.time_min < 0 || trans.time_max < trans.time_min {
                *error = format!("Invalid time range for transition: {}", trans.id);
                return false;
            }
            let interval =
                TimeInterval::new(trans.time_min, trans.time_max, trans.left_open, trans.right_open);
            let interval = match interval {
                Ok(i) => i,
                Err(e) => {
                    *error = format!("Invalid or empty integer time range for transition: {}", trans.id);
                    let _ = e;
                    return false;
                }
            };
            if !interval.is_valid() {
                *error = format!("Invalid or empty integer time range for transition: {}", trans.id);
                return false;
            }
        }

        let mut node_is_place: HashMap<String, bool> = HashMap::new();
        for place in &ast.places {
            node_is_place.insert(place.id.clone(), true);
        }
        for trans in &ast.transitions {
            node_is_place.insert(trans.id.clone(), false);
        }

        for arc in &ast.arcs {
            match node_is_place.get(&arc.source) {
                None => {
                    *error = format!("Arc source not found: {}", arc.source);
                    return false;
                }
                Some(_) => {}
            }
            match node_is_place.get(&arc.target) {
                None => {
                    *error = format!("Arc target not found: {}", arc.target);
                    return false;
                }
                Some(_) => {}
            }
            if node_is_place[&arc.source] == node_is_place[&arc.target] {
                *error = format!(
                    "Invalid arc (place->place or transition->transition): {} -> {}",
                    arc.source, arc.target
                );
                return false;
            }
            if arc.weight <= 0 {
                *error = format!("Arc weight must be positive: {} -> {}", arc.source, arc.target);
                return false;
            }
        }

        for init in &ast.initial_marking {
            if !place_ids.contains(&init.place) {
                *error = format!("Initial marking references unknown place: {}", init.place);
                return false;
            }
            if init.tokens < 0 {
                *error = format!("Negative token count for place: {}", init.place);
                return false;
            }
        }

        true
    }

    pub fn parse(input: &str, ast: &mut PTPNAST, error: &mut String) -> bool {
        *ast = PTPNAST::default();
        let mut pos = 0;

        skip_whitespace_and_comments(input, &mut pos);

        while pos < input.len() {
            skip_whitespace_and_comments(input, &mut pos);
            if pos >= input.len() {
                break;
            }
            let bytes: Vec<char> = input.chars().collect();

            if bytes[pos] == '@' {
                pos += 1;
                let directive = read_identifier(input, &mut pos);

                if directive == "init" {
                    skip_whitespace_and_comments(input, &mut pos);
                    loop {
                        if pos >= input.len() || (!bytes[pos].is_alphabetic() && bytes[pos] != '_')
                        {
                            break;
                        }
                        let mut init = InitNode {
                            place: read_identifier(input, &mut pos),
                            tokens: 1,
                        };
                        if init.place.is_empty() {
                            break;
                        }
                        skip_whitespace_and_comments(input, &mut pos);
                        if pos < input.len() && bytes[pos] == ':' {
                            pos += 1;
                            init.tokens = read_number(input, &mut pos);
                        }
                        ast.initial_marking.push(init);
                        skip_whitespace_and_comments(input, &mut pos);
                        if pos < input.len() && bytes[pos] == ',' {
                            pos += 1;
                            skip_whitespace_and_comments(input, &mut pos);
                        } else {
                            break;
                        }
                    }
                } else if directive == "capacity" {
                    skip_whitespace_and_comments(input, &mut pos);
                    loop {
                        if pos >= input.len() || (!bytes[pos].is_alphabetic() && bytes[pos] != '_')
                        {
                            break;
                        }
                        let place_id = read_identifier(input, &mut pos);
                        skip_whitespace_and_comments(input, &mut pos);
                        if pos < input.len() && bytes[pos] == ':' {
                            pos += 1;
                            let cap = read_number(input, &mut pos);
                            for p in ast.places.iter_mut() {
                                if p.id == place_id {
                                    p.capacity = cap;
                                    break;
                                }
                            }
                        }
                        skip_whitespace_and_comments(input, &mut pos);
                        if pos < input.len() && bytes[pos] == ',' {
                            pos += 1;
                            skip_whitespace_and_comments(input, &mut pos);
                        } else {
                            break;
                        }
                    }
                }
                continue;
            }

            let token = read_identifier(input, &mut pos);

            if token == "places" {
                skip_whitespace_and_comments(input, &mut pos);
                loop {
                    if pos >= input.len() {
                        break;
                    }
                    let bytes: Vec<char> = input.chars().collect();
                    if bytes[pos] == '@' {
                        break;
                    }
                    if bytes[pos] == 't' || bytes[pos] == 'T' {
                        let mut lookahead = pos;
                        let kw = read_identifier(input, &mut lookahead);
                        if kw == "transitions" {
                            break;
                        }
                    }
                    if !bytes[pos].is_alphabetic() && bytes[pos] != '_' {
                        pos += 1;
                        skip_whitespace_and_comments(input, &mut pos);
                        continue;
                    }

                    let mut place = PlaceNode {
                        id: read_identifier(input, &mut pos),
                        capacity: 1,
                        name: String::new(),
                    };
                    if place.id.is_empty() {
                        pos += 1;
                        continue;
                    }

                    skip_whitespace_and_comments(input, &mut pos);
                    if pos < input.len() && bytes[pos] == ':' {
                        pos += 1;
                        skip_whitespace_and_comments(input, &mut pos);
                        if pos < input.len() && bytes[pos].is_digit(10) {
                            place.capacity = read_number(input, &mut pos);
                        } else {
                            place.name = read_identifier(input, &mut pos);
                            skip_whitespace_and_comments(input, &mut pos);
                            if pos < input.len() && bytes[pos] == ':' {
                                pos += 1;
                                skip_whitespace_and_comments(input, &mut pos);
                                place.capacity = read_number(input, &mut pos);
                            }
                        }
                    }

                    ast.places.push(place);
                    skip_whitespace_and_comments(input, &mut pos);
                    if pos < input.len() && bytes[pos] == ',' {
                        pos += 1;
                        skip_whitespace_and_comments(input, &mut pos);
                    }
                }
            } else if token == "transitions" {
                skip_whitespace_and_comments(input, &mut pos);
                loop {
                    if pos >= input.len() {
                        break;
                    }
                    skip_whitespace_and_comments(input, &mut pos);
                    if pos >= input.len() {
                        break;
                    }
                    let bytes: Vec<char> = input.chars().collect();
                    if bytes[pos] == '@' {
                        break;
                    }
                    if bytes[pos].is_alphabetic() || bytes[pos] == '_' {
                        let mut lookahead_it = pos;
                        let src = read_identifier(input, &mut lookahead_it);
                        skip_whitespace_and_comments(input, &mut lookahead_it);
                        if lookahead_it + 1 < input.chars().count() && bytes.get(lookahead_it) == Some(&'-') {
                            let lookahead_bytes: Vec<char> = input.chars().collect();
                            if lookahead_bytes.get(lookahead_it + 1) == Some(&'>') {
                                break;
                            }
                        }
                        let _ = src;
                    }
                    if !bytes[pos].is_alphabetic() && bytes[pos] != '_' && bytes[pos] != '['
                        && bytes[pos] != '(' && bytes[pos] != '@'
                    {
                        pos += 1;
                        continue;
                    }

                    let mut trans = TransitionNode::default();
                    trans.id = read_identifier(input, &mut pos);
                    if trans.id.is_empty() {
                        pos += 1;
                        continue;
                    }

                    skip_whitespace_and_comments(input, &mut pos);
                    if pos < input.len() && bytes[pos] == ':' {
                        pos += 1;
                        skip_whitespace_and_comments(input, &mut pos);
                        trans.name = read_identifier(input, &mut pos);
                        skip_whitespace_and_comments(input, &mut pos);
                    }

                    if pos < input.len() && (bytes[pos] == '[' || bytes[pos] == '(') {
                        trans.left_open = bytes[pos] == '(';
                        pos += 1;
                        skip_whitespace_and_comments(input, &mut pos);
                        trans.time_min = read_number(input, &mut pos);
                        skip_whitespace_and_comments(input, &mut pos);
                        if pos < input.len() && bytes[pos] == ',' {
                            pos += 1;
                            skip_whitespace_and_comments(input, &mut pos);
                            trans.time_max = read_number(input, &mut pos);
                        }
                        skip_whitespace_and_comments(input, &mut pos);
                        if pos < input.len() && (bytes[pos] == ']' || bytes[pos] == ')') {
                            trans.right_open = bytes[pos] == ')';
                            pos += 1;
                        }
                    }

                    let done_with_transition = false;
                    while !done_with_transition && pos < input.len() {
                        skip_whitespace_and_comments(input, &mut pos);
                        if pos >= input.len() {
                            break;
                        }
                        let bytes: Vec<char> = input.chars().collect();
                        if bytes[pos] == '@' {
                            if !parse_transition_attribute(&mut trans, input, &mut pos) {
                                break;
                            }
                            skip_whitespace_and_comments(input, &mut pos);
                            if pos < input.len() && bytes[pos] == ',' {
                                pos += 1;
                            }
                        } else if parse_bare_transition_attribute(&mut trans, input, &mut pos) {
                            skip_whitespace_and_comments(input, &mut pos);
                            if pos < input.len() && bytes[pos] == ',' {
                                pos += 1;
                            }
                            if trans.suspendable {
                                break;
                            }
                        } else if bytes[pos].is_alphabetic() || bytes[pos] == '_' {
                            break;
                        } else {
                            pos += 1;
                        }
                    }

                    ast.transitions.push(trans);
                }
            } else if !token.is_empty() {
                let src = token;
                skip_whitespace_and_comments(input, &mut pos);
                let bytes: Vec<char> = input.chars().collect();
                if pos + 1 < bytes.len() && bytes[pos] == '-' && bytes[pos + 1] == '>' {
                    pos += 2;
                    skip_whitespace_and_comments(input, &mut pos);
                    let tgt = read_identifier(input, &mut pos);
                    let mut arc = ArcNode {
                        source: src,
                        target: tgt,
                        weight: 1,
                    };
                    skip_whitespace_and_comments(input, &mut pos);
                    if pos < input.len() && bytes[pos] == ':' {
                        pos += 1;
                        skip_whitespace_and_comments(input, &mut pos);
                        arc.weight = read_number(input, &mut pos);
                    }
                    ast.arcs.push(arc);
                }
            } else {
                pos += 1;
            }
        }

        Self::validate(ast, error)
    }

    pub fn parse_file(filepath: &str, ast: &mut PTPNAST, error: &mut String) -> bool {
        match std::fs::read_to_string(filepath) {
            Ok(content) => Self::parse(&content, ast, error),
            Err(e) => {
                *error = format!("Cannot open file: {} ({})", filepath, e);
                false
            }
        }
    }
}

pub struct PTPNBuilder;

impl PTPNBuilder {
    pub fn build(ast: &PTPNAST) -> PTPN {
        let mut ptpn = PTPN::new();
        let mut place_index: HashMap<String, usize> = HashMap::new();
        let mut transition_index: HashMap<String, usize> = HashMap::new();

        for (i, p) in ast.places.iter().enumerate() {
            place_index.insert(p.id.clone(), i);
            let name = if p.name.is_empty() { p.id.clone() } else { p.name.clone() };
            ptpn.add_place(&name, p.capacity, false);
        }

        for (i, t) in ast.transitions.iter().enumerate() {
            transition_index.insert(t.id.clone(), i);
            let name = if t.name.is_empty() { t.id.clone() } else { t.name.clone() };
            let interval = TimeInterval::new(t.time_min, t.time_max, t.left_open, t.right_open)
                .unwrap_or_else(|_| TimeInterval::closed(t.time_min, t.time_max));
            ptpn.add_transition(&name, interval, t.priority, t.core, t.suspendable);
        }

        for arc in &ast.arcs {
            if let (Some(&src_place), Some(&tgt_trans)) =
                (place_index.get(&arc.source), transition_index.get(&arc.target))
            {
                ptpn.set_pre_arc(src_place, tgt_trans, arc.weight);
            } else if let (Some(&src_trans), Some(&tgt_place)) =
                (transition_index.get(&arc.source), place_index.get(&arc.target))
            {
                ptpn.set_post_arc(src_trans, tgt_place, arc.weight);
            }
        }

        let mut marking = vec![0; ptpn.num_places()];
        for init in &ast.initial_marking {
            if let Some(&idx) = place_index.get(&init.place) {
                marking[idx] = init.tokens;
            }
        }
        ptpn.set_initial_marking_vec(marking);

        ptpn
    }

    pub fn parse(source: &str) -> Result<PTPN, String> {
        let mut ast = PTPNAST::default();
        let mut error = String::new();
        if !PTPNParser::parse(source, &mut ast, &mut error) {
            return Err(error);
        }
        Ok(Self::build(&ast))
    }

    pub fn parse_file(filepath: &str) -> Result<PTPN, String> {
        let mut ast = PTPNAST::default();
        let mut error = String::new();
        if !PTPNParser::parse_file(filepath, &mut ast, &mut error) {
            return Err(error);
        }
        Ok(Self::build(&ast))
    }
}
