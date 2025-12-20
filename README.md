## USER MANUAL:
- **Inequalities:** <, <=, >, >=
- **Comparisons:** or (||), and (&&), xor (^), set difference (\)

---

## IMPORTANT KEYS:
- **Enter:** Makes you go onto a new line. If you press enter at the start of the string, the new line will appear above. Anywhere else, it 
will appear below. 
- **Left and right arrow keys:** Move between letters inside a line
- **Up and down arrow keys:** Move between lines (up or down)
- **Backspace:** remove the letter youre currently at (shown by the white vertical line)
- **Tab:** delete a line
- **Any key:** it types a letter
  
---

## There are 3 different expressions you can input: -comp, -var, -func
- How your expressions should look like:
- **comp:** comp (name1) (comparator) (name2) -> comp a || b
- **var:** var (name) = (expression) -> var a = b^2 * 2 + 5
- **func:** func (name): (equation) -> func a: y = x
- **Usable operations:**
- binary (two arguements): +; -; *; /; ^; % (mod);
- unary (single arguement): abs(); ln(); log(); sqrt(); sin(); cos(); tan(); asin(); acos(); atan(); floor();
- **Constants you can use:** 
- e (2.7182818)
- pi (3.141592)

- You cannot compare more than 2 functions at once
- The expressions can be essentially in any form. Here are some examples:
- "y = x", "y - x = 0", "x = 5", "y = 5", "0 = 1", "a + b + c + d = 0"
- -> for the last one, you need to set the variables, like: var a = 5, var b = 2, etc...
- Also, when comparing two functions, you have to use the name you gave your function, so for example:
- func a = y = x,  func b = y = -x, comp a || b
