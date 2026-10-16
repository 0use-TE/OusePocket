"use strict";

let expr = "";
const MAX_EXPR = 48;

function render(answer) {
    Ouse.ui.setText("expr", expr || "0");
    Ouse.ui.setText("result", answer);
}

function lastChar() {
    return expr.length ? expr.charAt(expr.length - 1) : "";
}

function trailingOperators() {
    let count = 0;
    for (let i = expr.length - 1; i >= 0; i--) {
        const ch = expr.charAt(i);
        if (ch !== "+" && ch !== "-" && ch !== "*" && ch !== "/") break;
        count++;
    }
    return count;
}

function currentNumber() {
    let end = expr.length;
    while (end > 0) {
        const ch = expr.charAt(end - 1);
        if ((ch < "0" || ch > "9") && ch !== ".") break;
        end--;
    }
    return expr.slice(end);
}

function openCount() {
    let count = 0;
    for (let i = 0; i < expr.length; i++) {
        const ch = expr.charAt(i);
        if (ch === "(") count++;
        else if (ch === ")") count--;
    }
    return count;
}

function endedValue() {
    const ch = lastChar();
    return (ch >= "0" && ch <= "9") || ch === ")" || ch === "%";
}

function isUnaryMinus(text, index) {
    if (text.charAt(index) !== "-") return false;
    if (index === 0) return true;
    const prev = text.charAt(index - 1);
    return prev === "+" || prev === "-" || prev === "*" || prev === "/" || prev === "(";
}

function atomStart(text, percentIndex) {
    let index = percentIndex - 1;
    if (text.charAt(index) === ")") {
        let depth = 1;
        index--;
        while (index >= 0 && depth) {
            const ch = text.charAt(index);
            if (ch === ")") depth++;
            else if (ch === "(") depth--;
            index--;
        }
        index++;
    } else {
        while (index >= 0) {
            const ch = text.charAt(index);
            if ((ch >= "0" && ch <= "9") || ch === ".") {
                index--;
                continue;
            }
            break;
        }
        index++;
    }
    if (index > 0 && isUnaryMinus(text, index - 1)) index--;
    return index;
}

function format(value) {
    if (typeof value !== "number" || value !== value || value === Infinity || value === -Infinity) return null;
    const rounded = Math.round(value * 100000000) / 100000000;
    const text = String(rounded);
    if (text.indexOf("e") >= 0 || text.indexOf("E") >= 0 || text.length > 16) return null;
    return text;
}

function parse(text) {
    const source = expandPercent(text);
    let index = 0;
    function peek() { return source.charAt(index); }
    function expression() {
        let value = term();
        while (peek() === "+" || peek() === "-") {
            const op = peek();
            index++;
            const right = term();
            value = op === "+" ? value + right : value - right;
        }
        return value;
    }
    function term() {
        let value = unary();
        while (peek() === "*" || peek() === "/") {
            const op = peek();
            index++;
            const right = unary();
            if (op === "/" && right === 0) throw new Error("Div/0");
            value = op === "*" ? value * right : value / right;
        }
        return value;
    }
    function unary() {
        if (peek() === "+") { index++; return unary(); }
        if (peek() === "-") { index++; return -unary(); }
        return primary();
    }
    function primary() {
        if (peek() === "(") {
            index++;
            const value = expression();
            if (peek() !== ")") throw new Error("Error");
            index++;
            return value;
        }
        return number();
    }
    function number() {
        const start = index;
        while (peek() >= "0" && peek() <= "9") index++;
        if (peek() === ".") {
            index++;
            if (peek() < "0" || peek() > "9") throw new Error("Error");
            while (peek() >= "0" && peek() <= "9") index++;
        }
        if (index === start) throw new Error("Error");
        return Number(source.slice(start, index));
    }
    const value = expression();
    if (index !== source.length) throw new Error("Error");
    return value;
}

function expandPercent(text) {
    const mark = text.indexOf("%");
    if (mark < 0) return text;
    const start = atomStart(text, mark);
    const atom = parse(text.slice(start, mark));
    const opIndex = start - 1;
    const op = opIndex >= 0 ? text.charAt(opIndex) : "";
    const prev = opIndex > 0 ? text.charAt(opIndex - 1) : "";
    const binaryAdd = (op === "+" || op === "-") && opIndex > 0 && prev !== "+" && prev !== "-" && prev !== "*" && prev !== "/" && prev !== "(";
    let from = start;
    let shown;
    if (binaryAdd) {
        shown = format(parse(text.slice(0, opIndex)) * atom / 100);
        from = opIndex;
    } else if (op === "*" || op === "/") {
        shown = format(atom / 100);
        from = opIndex;
    } else {
        shown = format(atom / 100);
    }
    if (shown === null) throw new Error("Error");
    const prefix = binaryAdd || op === "*" || op === "/" ? op + shown : shown;
    return expandPercent(text.slice(0, from) + prefix + text.slice(mark + 1));
}

function answerOf(text) {
    try {
        const shown = format(parse(text));
        return shown === null ? "Error" : shown;
    } catch (error) {
        return error.message === "Div/0" ? "Div/0" : "Error";
    }
}

function preview() {
    if (!expr) {
        render("0");
        return;
    }
    if (!endedValue() || openCount()) {
        render("");
        return;
    }
    render(answerOf(expr));
}

function append(ch) {
    if (expr.length >= MAX_EXPR) return;
    const ended = endedValue();
    const operators = trailingOperators();
    const number = currentNumber();
    if (ch >= "0" && ch <= "9") {
        if (lastChar() === ")" || lastChar() === "%") return;
        if (number === "0") {
            if (ch === "0") return;
            expr = expr.slice(0, -1) + ch;
        } else expr += ch;
    } else if (ch === ".") {
        if (!number || number.indexOf(".") >= 0) return;
        expr += ch;
    } else if (ch === "+" || ch === "*" || ch === "/") {
        if (!ended && operators !== 1) return;
        if (operators === 1) expr = expr.slice(0, -1);
        expr += ch;
    } else if (ch === "-") {
        if (lastChar() === "." || operators >= 2) return;
        expr += ch;
    } else if (ch === "(") {
        if (ended || lastChar() === ".") return;
        expr += ch;
    } else if (ch === ")") {
        if (!ended || !openCount()) return;
        expr += ch;
    } else if (ch === "%") {
        if (!ended) return;
        expr += "%";
    }
    preview();
}

function autoParen() {
    if (endedValue() && openCount()) {
        append(")");
        return;
    }
    if (endedValue()) {
        if (expr.length + 2 > MAX_EXPR) return;
        expr += "*(";
        preview();
        return;
    }
    append("(");
}

function clearAll() {
    expr = "";
    preview();
}

function backspace() {
    expr = expr.slice(0, -1);
    preview();
}

function equals() {
    if (!expr || !endedValue() || openCount()) {
        if (expr) render("Error");
        return;
    }
    const shown = answerOf(expr);
    if (shown === "Error" || shown === "Div/0") {
        render(shown);
        return;
    }
    expr = shown;
    render(shown);
}

const keys = {
    b_div: "/", b_mul: "*", b_sub: "-", b_add: "+",
    b_dot: ".",
    b_0: "0", b_1: "1", b_2: "2", b_3: "3", b_4: "4",
    b_5: "5", b_6: "6", b_7: "7", b_8: "8", b_9: "9"
};

Object.keys(keys).forEach((id) => {
    const token = keys[id];
    Ouse.ui.on(id, "click", () => append(token));
});
Ouse.ui.on("b_ac", "click", clearAll);
Ouse.ui.on("b_bs", "click", backspace);
Ouse.ui.on("b_eq", "click", equals);
Ouse.ui.on("b_paren", "click", autoParen);
Ouse.ui.on("b_pct", "click", () => append("%"));
preview();
