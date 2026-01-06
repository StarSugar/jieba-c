(declaim (optimize (speed 3) (space 0) (safety 0) (debug 0)))

(defun primes-upto (n)
  (declare (type fixnum n))
  (loop with primes = (list 2)
        for x from 3 to (+ 1 n) by 2
        for k = (floor (sqrt x))
    unless (some (lambda (y)
                   (and (<= y k)
                        (= 0 (nth-value 1 (floor x y)))))
                 primes)
    do (push x primes)
    finally (return-from primes-upto (nreverse primes))))

(defparameter *primes* (primes-upto 600000))

(defun find-primes ()
  (loop with ceils = (loop for x = 2047 then (* x 2)
                        while (<= x 600000)
                        collect x)
        with result = nil
        for prime in *primes*
        for next-prime in (cdr *primes*)
    when (and ceils (> next-prime (car ceils))) do
      (push prime result)
      (setf ceils (cdr ceils))
    finally (return-from find-primes (nreverse result))))

(format t "{~{~a~^, ~}}~%" (find-primes))
