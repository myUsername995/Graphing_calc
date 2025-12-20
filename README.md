## USER MANUAL:
- Inequalities: <, <=, >, >=
- Comparisons: or (||), and (&&), xor (^), set difference (\ or \\)

---

## IMPORTANT KEYS:
- Enter: Makes you go onto a new line. If you press enter at the start of the string, the new line will appear above. Anywhere else, it 
will appear below. 
- Left and right arrow keys: Move between letters inside a line
- Up and down arrow keys: Move between lines (up or down)
- Backspace: remove the letter youre currently at (shown by the white vertical line)
- Tab: delete a line
- Any key: it types a letter
  
---

## There are 3 different expressions you can input: -comp, -var, -func

## How your expressions should look like:
- comp: comp (name1) (comparator) (name2) -> comp a || b
- var: var (name) = (expression) -> var a = b^2 * 2 + 5
- func: func (name): (equation) -> func a: y = x

- You cannot compare more than 2 functions at once
- For the type of functions you can input, look at expression.cpp. Documentation is at the top.
- The expressions can be essentially in any form. Here are some examples:
- "y = x", "y - x = 0", "x = 5", "y = 5", "0 = 1", "a + b + c + d = 0"
- -> for the last one, you need to set the variables, like: var a = 5, var b = 2, etc...
- Also, when comparing two functions, you have to use the name you gave your function, so for example:
- func a = y = x,  func b = y = -x, comp a || b

## DOCUMENTATION:
- The renderer renders functions in 3 steps: 
- 1st: parsing user input
- 2nd: evaluating functions
- 3rd: combining functions

- I'm going to explain each step in detail here:
## 1st step
- This step happens everytime the user updates their expressions. The parsing happens inside of updateExprs(), where the three different 
- types of inputs are all handled. 
- The "var" kind is parsed by first finding the variable name, then parsing and evaluating the function 
- expression. Note that variables must be declared BEFORE they're used. 
- The "comp" kind is parsed by collecting all the names and comparators into an array, and after every functions is parsed, only then 
- does it compare them, so that every function can get evaluted before they're combined. You can put this expression anywhere and it will 
- work.
- The "func" kind is parsed by also collecting every function name and expression into an array, and it only starts going through all 
- the functions once the variables have all been parsed. Every elements gets passed to a function called getFunction(), which converts the 
- relationship the user inputted (e.g: y = x) to an actual equation (e.g: y - x). These are then evaluted.

- After all of these inputs have been parsed, theyre passed to the GPU using the createBuffersAndUpload() function.

## 2nd step
- This steps happens on every frame (because the user can move the camera around at any time). The expressions are converted into GPU 
- code, and then passed into the compute shader. After this the compute shader gets recompiled.

## 3rd step
- This step also happens on every frame, inside the runCompute() function. It's just a compute shader that compares each function per pixel, 
- and colors each pixel accordingly. Boundaries are handled specially, because they should only be drawn if theyre next to a shaded 
- pixel (otherwise when comparing 2 functions, the result might look silly).
